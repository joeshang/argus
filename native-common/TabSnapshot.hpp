#pragma once

#include <charconv>
#include <chrono>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include <nlohmann/json.hpp>
#include <sys/types.h>

namespace argus {

struct BrowserIdentity {
    pid_t pid = 0;
    uint64_t startTime = 0;
    auto operator<=>(const BrowserIdentity&) const = default;
};

struct FrameData {
    BrowserIdentity browser;
    std::string sourceId;
    int tabId = 0;
    int frameId = 0;
    int parentFrameId = -1;
    std::string documentId;
    int rendererClientId = 0;
    std::string url;
    bool isSubframe = false;
    bool withMetric = false;
    int webRequestDurationMs = 0;
    int pageLoadTotalDurationMs = 0;
    int pageLoadNetworkDurationMs = 0;
    int pageLoadRenderDurationMs = 0;
    int requestCount = 0;
    int httpStatusCount2xx = 0;
    int httpStatusCount3xx = 0;
    int httpStatusCount4xx = 0;
    int httpStatusCount5xx = 0;
};

// Caller owns synchronization. A socket connection is one profile's transport;
// complete snapshots atomically replace its frames, never a renderer's single URL.
class TabSnapshots {
public:
    using Clock = std::chrono::steady_clock;
    bool receive(uint64_t connectionId, const std::string& message, Clock::time_point now = Clock::now()) {
        if (connectionId == 0 || message.size() > 1024 * 1024) return false;
        auto value = nlohmann::json::parse(message, nullptr, false);
        if (!value.is_object() || value.value("type", nlohmann::json()) != "snapshot" ||
            value.value("protocolVersion", nlohmann::json()) != 2) return false;
        try {
            const auto browser = value.at("browser").get<std::string>();
            const auto source = value.at("sourceId").get<std::string>();
            const auto start = value.at("browserStartTime").get<std::string>();
            BrowserIdentity identity;
            identity.pid = integer(value.at("browserPid"), 1);
            const auto converted = std::from_chars(start.data(), start.data() + start.size(), identity.startTime);
            if (converted.ec != std::errc() || converted.ptr != start.data() + start.size() || identity.startTime == 0 ||
                (browser != "chrome" && browser != "edge") || source.empty() || source.size() > 128) return false;
            const auto generation = value.at("generation");
            if (!generation.is_number_integer() || generation <= 0 || generation > 9007199254740991ULL) return false;
            const uint64_t sequence = generation.get<uint64_t>();
            const int count = integer(value.at("partCount"), 1, 16);
            const int index = integer(value.at("partIndex"), 0, count - 1);
            const auto& records = value.at("frames");
            if (!records.is_array() || records.size() > 10000) return false;
            std::vector<FrameData> frames;
            frames.reserve(records.size());
            for (const auto& record : records) frames.push_back(decode(record, identity, source));
            if (!m_connections.contains(connectionId) && m_connections.size() >= 64) return false;
            const bool fresh = !m_connections.contains(connectionId);
            auto& connection = m_connections[connectionId];
            if (fresh) connection.updated = now;
            if (connection.retired || (connection.identity.pid != 0 &&
                (connection.identity != identity || connection.source != source || connection.browser != browser))) return false;
            connection.identity = identity;
            connection.source = source;
            connection.browser = browser;
            if (sequence <= connection.sequence) return false;
            if (index == 0) {
                if (connection.pending && sequence <= connection.pending->sequence) return false;
                connection.pending = Pending { sequence, count, 0, 0, now, {} };
            }
            if (!connection.pending || connection.pending->sequence != sequence ||
                connection.pending->count != count || connection.pending->next != index) return false;
            auto& pending = *connection.pending;
            if (pending.bytes + message.size() > 4 * 1024 * 1024 || pending.frames.size() + frames.size() > 10000 ||
                now - pending.started > std::chrono::seconds(5)) {
                connection.pending.reset();
                return false;
            }
            pending.bytes += message.size();
            pending.frames.insert(pending.frames.end(), std::make_move_iterator(frames.begin()), std::make_move_iterator(frames.end()));
            if (++pending.next == count) {
                std::set<std::pair<int, int>> keys;
                for (const auto& frame : pending.frames) {
                    if (!keys.emplace(frame.tabId, frame.frameId).second) { connection.pending.reset(); return false; }
                }
                connection.frames = std::move(pending.frames);
                connection.sequence = sequence;
                connection.updated = now;
                connection.pending.reset();
                // A newer port for the same profile supersedes older ports,
                // even if their EOF is processed after this snapshot arrives.
                for (auto& [id, other] : m_connections) {
                    if (id < connectionId && other.identity == identity && other.browser == browser && other.source == source) {
                        other.retired = true;
                        other.frames.clear();
                        other.pending.reset();
                    }
                    if (id > connectionId && !other.retired && other.sequence > 0 &&
                        other.identity == identity && other.browser == browser && other.source == source) {
                        connection.retired = true;
                        connection.frames.clear();
                    }
                }
                rebuild();
            }
            return true;
        } catch (const nlohmann::json::exception&) { return false; }
          catch (const std::invalid_argument&) { return false; }
    }

    std::vector<FrameData> frames(BrowserIdentity identity, const std::string& browser, int renderer) const {
        std::vector<FrameData> result;
        const auto found = m_index.find({ identity, browser, renderer });
        if (found != m_index.end()) for (const auto* frame : found->second) result.push_back(*frame);
        return result;
    }
    void disconnect(uint64_t id) { m_connections.erase(id); rebuild(); }
    void expire(Clock::time_point now = Clock::now()) {
        std::erase_if(m_connections, [&](const auto& item) { return now - item.second.updated > std::chrono::seconds(15); });
        for (auto& [id, value] : m_connections) {
            if (value.pending && now - value.pending->started > std::chrono::seconds(5)) value.pending.reset();
        }
        rebuild();
    }
    void retain(const std::set<BrowserIdentity>& live) {
        std::erase_if(m_connections, [&](const auto& item) { return !live.contains(item.second.identity); });
        rebuild();
    }
private:
    static int integer(const nlohmann::json& value, int lower, int upper = INT32_MAX) {
        if (!value.is_number_integer() || value < lower || value > upper) throw std::invalid_argument("Invalid integer");
        return value.get<int>();
    }
    static FrameData decode(const nlohmann::json& value, BrowserIdentity browser, const std::string& source) {
        FrameData frame;
        frame.browser = browser;
        frame.sourceId = source;
        frame.tabId = integer(value.at("tabId"), 0);
        frame.frameId = integer(value.at("frameId"), 0);
        frame.parentFrameId = integer(value.at("parentFrameId"), -1);
        frame.rendererClientId = integer(value.at("rendererClientId"), 1);
        frame.documentId = value.at("documentId").get<std::string>();
        frame.url = value.at("url").get<std::string>();
        frame.isSubframe = value.at("isSubframe").get<bool>();
        frame.withMetric = value.value("withMetric", false);
        if (frame.documentId.empty() || frame.documentId.size() > 128 || frame.url.size() > 65536 ||
            (!frame.url.starts_with("http://") && !frame.url.starts_with("https://")) ||
            frame.isSubframe != (frame.frameId != 0) || (frame.withMetric && frame.isSubframe)) throw std::invalid_argument("Invalid frame");
        auto metric = [&](const char* key) { return value.contains(key) ? integer(value.at(key), 0) : 0; };
        frame.webRequestDurationMs = metric("webRequestDurationMs");
        frame.pageLoadTotalDurationMs = metric("pageLoadTotalDurationMs");
        frame.pageLoadNetworkDurationMs = metric("pageLoadNetworkDurationMs");
        frame.pageLoadRenderDurationMs = metric("pageLoadRenderDurationMs");
        frame.requestCount = metric("requestCount");
        frame.httpStatusCount2xx = metric("httpStatusCount2xx");
        frame.httpStatusCount3xx = metric("httpStatusCount3xx");
        frame.httpStatusCount4xx = metric("httpStatusCount4xx");
        frame.httpStatusCount5xx = metric("httpStatusCount5xx");
        return frame;
    }
    struct Pending {
        uint64_t sequence;
        int count, next;
        size_t bytes;
        Clock::time_point started;
        std::vector<FrameData> frames;
    };
    struct Connection {
        BrowserIdentity identity;
        std::string browser, source;
        uint64_t sequence = 0;
        bool retired = false;
        Clock::time_point updated;
        std::optional<Pending> pending;
        std::vector<FrameData> frames;
    };
    void rebuild() {
        m_index.clear();
        for (const auto& [id, connection] : m_connections) {
            if (!connection.retired) for (const auto& frame : connection.frames) {
                m_index[{ connection.identity, connection.browser, frame.rendererClientId }].push_back(&frame);
            }
        }
    }
    std::map<uint64_t, Connection> m_connections;
    std::map<std::tuple<BrowserIdentity, std::string, int>, std::vector<const FrameData*>> m_index;
};

} // namespace argus
