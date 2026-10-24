//
//  ProcessMonitor.cpp
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/11/27.
//

// 1. list all pids
// 2. get process name by pid
// 3. filter Chrome and Safari subprocess and get its performance
// 4. Chrome
//    * analysis Chrome's subprocess launch argument
//    * fetch rendererClientId for arguments
//    * get url of subprocess via renderClientId
// 5. Safari
//    * get host url of subprocess via private api

#include "BrowserMonitor.hpp"
#include "ProcessUtils.hpp"
#include "../../../native-common/BrowserProcesses.hpp"
#include <charconv>
#include <set>

#include <libproc.h>
#include <chrono>
#include <limits>
#include <vector>
#include <string>
#include <sstream>
#include <nlohmann/json.hpp>

extern "C" {
extern pid_t responsibility_get_pid_responsible_for_pid(pid_t);
}

BrowserMonitor::~BrowserMonitor() {
    stopMetric();
}

void BrowserMonitor::stopMetric() {
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
    }
    m_wakeup.notify_all();
    if (m_sampler.joinable()) m_sampler.join();
}

void BrowserMonitor::beginMetric(int interval, std::function<void(BrowserSnapshot)> callback) {
    if (interval <= 0 || !callback) throw std::invalid_argument("Invalid sampling interval or callback");
    stopMetric();
    {
        std::lock_guard lock(m_mutex);
        m_usage.clear();
        m_stopping = false;
    }
    m_sampler = std::thread([this, interval, callback = std::move(callback)] {
        std::unique_lock lock(m_mutex);
        while (!m_stopping) {
            queryBrowserProcesses();
            fetchBrowserProcessUsage();
            auto data = snapshotLocked();
            // Never call UI/client code while holding the monitor state lock.
            lock.unlock();
            callback(std::move(data));
            lock.lock();
            m_wakeup.wait_for(lock, std::chrono::seconds(interval), [this] { return m_stopping; });
        }
    });
}

BrowserSnapshot BrowserMonitor::snapshotLocked() const {
    BrowserSnapshot result { m_chromeProcesses, m_edgeProcesses, m_safariProcesses, {}, {}, {} };
    auto metrics = [](const auto& processes) {
        std::vector<WebMetricData> values;
        for (const auto& process : processes) for (const auto& frame : process.frames) {
            if (!frame.isSubframe && frame.withMetric) values.push_back(frame);
        }
        return values;
    };
    result.chromeWebMetric = metrics(m_chromeProcesses);
    result.edgeWebMetric = metrics(m_edgeProcesses);
    return result;
}

BrowserSnapshot BrowserMonitor::snapshot() const {
    std::lock_guard lock(m_mutex);
    return snapshotLocked();
}

void BrowserMonitor::receiveMessage(uint64_t connectionId, const std::string& message) {
    std::lock_guard lock(m_mutex);
    m_tabs.receive(connectionId, message);
}

void BrowserMonitor::disconnect(uint64_t connectionId) {
    std::lock_guard lock(m_mutex);
    m_tabs.disconnect(connectionId);
}

void BrowserMonitor::queryBrowserProcesses() {
    const auto records = argus::readProcessRecords();
    std::map<pid_t, argus::BrowserRoot> roots;
    std::set<argus::BrowserIdentity> live;
    std::vector<ChromiumProcessInfo> chrome, edge;
    std::vector<SafariProcessInfo> safari;
    for (const auto& [pid, record] : records) {
        const auto kind = argus::browserKind(record.name);
        if (kind.empty()) continue;
        const auto sample = argus::readProcessSample(pid);
        if (!sample) continue;
        const argus::BrowserIdentity identity { pid, sample->startTime };
        roots.emplace(pid, argus::BrowserRoot { identity, kind });
        live.insert(identity);
        if (kind == "safari") {
            SafariProcessInfo info {};
            info.pid = pid;
            info.type = SafariProcessType::Browser;
            safari.push_back(info);
        } else {
            ChromiumProcessInfo info;
            info.pid = pid;
            info.browser = identity;
            info.type = ChromiumProcessType::Browser;
            (kind == "chrome" ? chrome : edge).push_back(info);
        }
    }
    m_tabs.expire();
    m_tabs.retain(live);
    for (const auto& [pid, record] : records) {
        if (roots.contains(pid)) continue;
        const auto root = argus::browserAncestor(record.parent, records, roots);
        const bool chromiumHelper = record.name.find("Helper") != std::string::npos &&
            (record.name.starts_with("Google Chrome") || record.name.starts_with("Microsoft Edge") || record.name.starts_with("Chromium"));
        if (root && root->kind != "safari" && chromiumHelper) {
            ChromiumProcessInfo info;
            info.pid = pid;
            info.browser = root->identity;
            handleChromiumProcessArguments(info);
            info.frames = m_tabs.frames(root->identity, root->kind, info.rendererClientId);
            std::set<std::string> urls;
            bool onlySubframes = !info.frames.empty();
            for (const auto& frame : info.frames) {
                urls.insert(frame.url);
                onlySubframes = onlySubframes && frame.isSubframe;
            }
            for (const auto& url : urls) {
                if (!info.url.empty()) info.url += " | ";
                info.url += url;
            }
            if (onlySubframes) info.type = ChromiumProcessType::Subframe;
            (root->kind == "chrome" ? chrome : edge).push_back(std::move(info));
        } else if (record.name == "com.apple.WebKit.WebContent" || record.name == "com.apple.WebKit.GPU" ||
                   record.name == "com.apple.WebKit.Networking") {
            const auto responsible = roots.find(responsibility_get_pid_responsible_for_pid(pid));
            if (responsible == roots.end() || responsible->second.kind != "safari") continue;
            SafariProcessInfo info {};
            info.pid = pid;
            if (record.name == "com.apple.WebKit.WebContent") {
                info.type = SafariProcessType::Tab;
                info.url = processUtilsGetWebKitActiveUrl(pid);
            } else info.type = record.name == "com.apple.WebKit.GPU" ? SafariProcessType::GPU : SafariProcessType::Network;
            safari.push_back(std::move(info));
        }
    }
    auto order = [](auto& processes) { std::sort(processes.begin(), processes.end(), [](const auto& a, const auto& b) { return a.pid < b.pid; }); };
    order(chrome); order(edge); order(safari);
    m_chromeProcesses = std::move(chrome);
    m_edgeProcesses = std::move(edge);
    m_safariProcesses = std::move(safari);
}

void BrowserMonitor::fetchBrowserProcessUsage() {
    std::unordered_set<pid_t> live;

    for (auto &info: m_chromeProcesses) {
        info.usage = getProcessUsage(info.pid);
        live.insert(info.pid);
    }

    for (auto &info: m_edgeProcesses) {
        info.usage = getProcessUsage(info.pid);
        live.insert(info.pid);
    }

    for (auto &info: m_safariProcesses) {
        info.usage = getProcessUsage(info.pid);
        live.insert(info.pid);
    }

    m_usage.retain(live);
}

void BrowserMonitor::printUsage() {
    std::lock_guard lock(m_mutex);
    std::cout << "============ Chrome ===========\n"
    << std::fixed << std::setw(10) << "pid" << "\t"
    << std::fixed << std::setw(10) << "cpu" << "\t"
    << std::fixed << std::setw(10) << "mem" << "\t"
    << std::fixed << std::setw(10) << "type" << "\t"
    << std::fixed << std::setw(20) << "renderId" << "\t"
    << "url" << "\n";
    for (const auto &info: m_chromeProcesses) {
        std::cout << info;
    }
    std::cout << "\n";

    std::cout << "============ Edge ===========\n"
    << std::fixed << std::setw(10) << "pid" << "\t"
    << std::fixed << std::setw(10) << "cpu" << "\t"
    << std::fixed << std::setw(10) << "mem" << "\t"
    << std::fixed << std::setw(10) << "type" << "\t"
    << std::fixed << std::setw(20) << "renderId" << "\t"
    << "url" << "\n";
    for (const auto &info: m_edgeProcesses) {
        std::cout << info;
    }
    std::cout << "\n";

    std::cout << "============ Safari ===========\n"
    << std::fixed << std::setw(10) << "pid" << "\t"
    << std::fixed << std::setw(10) << "cpu" << "\t"
    << std::fixed << std::setw(10) << "mem" << "\t"
    << std::fixed << std::setw(20) << "type" << "\t"
    << std::fixed << "url" << "\n";
    for (const auto &info: m_safariProcesses) {
        std::cout << info;
    }

    std::cout << "\n\n";
}

UsageData BrowserMonitor::getProcessUsage(pid_t pid) {
    return m_usage.sample(pid);
}

void BrowserMonitor::handleChromiumProcessArguments(ChromiumProcessInfo &info) {
    std::vector<std::string> arguments;
    processUtilsGetProcessArguments(info.pid, &arguments);

    bool isExtension = false;
    bool isPrerender = false;
    for (auto &argument: arguments) {
        if (argument == "--type=renderer") {
            info.type = ChromiumProcessType::Tab;
        } else if (argument == "--type=gpu-process") {
            info.type = ChromiumProcessType::GPUProcess;
        } else if (argument == "--utility-sub-type=network.mojom.NetworkService") {
            info.type = ChromiumProcessType::NetworkService;
        } else if (argument == "--utility-sub-type=storage.mojom.StorageService") {
            info.type = ChromiumProcessType::StorageService;
        } else if (argument == "--utility-sub-type=audio.mojom.AudioService") {
            info.type = ChromiumProcessType::AudioService;
        } else if (argument == "--utility-sub-type=asset_store.mojom.AssetStoreService") {
            info.type = ChromiumProcessType::AssetStoreService;
        } else if (argument == "--utility-sub-type=data_decoder.mojom.DataDecoderService") {
            info.type = ChromiumProcessType::DataDecoderService;
        } else if (argument == "--utility-sub-type=proxy_resolver.mojom.ProxyResolverFactory") {
            info.type = ChromiumProcessType::V8ProxyResolver;
        } else if (argument == "--extension-process") {
            isExtension = true;
        } else if (argument == "--instant-process") {
            // Edge's prerender tab will have this argument
            isPrerender = true;
        } else if (argument.starts_with("--renderer-client-id=")) {
            std::string prefix = "--renderer-client-id=";
            std::string clientId = argument.substr(prefix.size(), argument.size() - prefix.size());
            int parsed = 0;
            const auto result = std::from_chars(clientId.data(), clientId.data() + clientId.size(), parsed);
            if (result.ec == std::errc() && result.ptr == clientId.data() + clientId.size() && parsed > 0) info.rendererClientId = parsed;
        }
    }

    if (info.type == ChromiumProcessType::Tab) {
        if (isExtension) {
            info.type = ChromiumProcessType::Extension;
        } else if (isPrerender) {
            info.type = ChromiumProcessType::PrerenderTab;
        }
    }
}


std::string typeString(ChromiumProcessType processType) {
    std::string type {};
    switch (processType) {
        case ChromiumProcessType::Other:
            type = "Other";
            break;
        case ChromiumProcessType::Browser:
            type = "Browser";
            break;
        case ChromiumProcessType::Tab:
            type = "Tab";
            break;
        case ChromiumProcessType::Subframe:
            type = "Subframe";
            break;
        case ChromiumProcessType::PrerenderTab:
            type = "Prerender Tab";
            break;
        case ChromiumProcessType::Extension:
            type = "Extension";
            break;
        case ChromiumProcessType::GPUProcess:
            type = "GPU Process";
            break;
        case ChromiumProcessType::AudioService:
            type = "Audio Service";
            break;
        case ChromiumProcessType::NetworkService:
            type = "Network Service";
            break;
        case ChromiumProcessType::StorageService:
            type = "Storage Service";
            break;
        case ChromiumProcessType::AssetStoreService:
            type = "Asset Store Service";
            break;
        case ChromiumProcessType::DataDecoderService:
            type = "Data Decoder Service";
            break;
        case ChromiumProcessType::V8ProxyResolver:
            type = "V8 Proxy Resolver";
            break;
        default:
            break;
    }
    return type;
}

std::string typeString(SafariProcessType processType) {
    std::string type {};
    switch (processType) {
        case SafariProcessType::Browser:
            type = "Browser";
            break;
        case SafariProcessType::Tab:
            type = "Tab";
            break;
        case SafariProcessType::GPU:
            type = "GPU";
            break;
        case SafariProcessType::Network:
            type = "Network";
            break;
        default:
            break;
    }
    return type;
}
