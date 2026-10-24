#include "../native-common/BrowserProcesses.hpp"
#include <cassert>
#include <iostream>

using nlohmann::json;
using Store = argus::TabSnapshots;
json frame(int tab = 1, int id = 0, int renderer = 7) {
    return {{"tabId", tab}, {"frameId", id}, {"parentFrameId", id == 0 ? -1 : 0},
        {"documentId", "document-" + std::to_string(tab) + "-" + std::to_string(id)},
        {"rendererClientId", renderer}, {"url", "https://example.com/" + std::to_string(tab)},
        {"isSubframe", id != 0}, {"withMetric", id == 0}, {"requestCount", 3}};
}
json snapshot(json frames, int generation = 1, int pid = 100, std::string start = "10", std::string source = "profile-a") {
    return {{"type", "snapshot"}, {"protocolVersion", 2}, {"browser", "chrome"}, {"browserPid", pid},
        {"browserStartTime", start}, {"sourceId", source}, {"generation", generation},
        {"partIndex", 0}, {"partCount", 1}, {"frames", std::move(frames)}};
}
int main() {
    using namespace std::chrono;
    const Store::Clock::time_point now(seconds(100));
    Store store;
    assert(store.receive(1, snapshot(json::array({frame(1), frame(2), frame(1, 2)})).dump(), now));
    assert(store.frames({100, 10}, "chrome", 7).size() == 3); // All tabs/frames sharing a renderer.
    assert(store.receive(2, snapshot(json::array({frame()}), 1, 200, "20").dump(), now));
    assert(store.frames({200, 20}, "chrome", 7).size() == 1); // Identical renderer/tab IDs, distinct root.
    assert(store.frames({100, 11}, "chrome", 7).empty()); // Root PID reuse cannot bind old data.
    assert(store.receive(3, snapshot(json::array({frame()}), 1, 100, "10", "profile-b").dump(), now));
    assert(store.frames({100, 10}, "chrome", 7).size() == 4); // Identical tab IDs in another profile.
    assert(store.receive(1, snapshot(json::array(), 2).dump(), now));
    assert(store.frames({100, 10}, "chrome", 7).size() == 1); // Empty snapshot removes closed tabs.
    store.disconnect(3);
    assert(store.frames({100, 10}, "chrome", 7).empty());

    auto part = snapshot(json::array({frame(3)}), 3);
    part["partCount"] = 2;
    assert(store.receive(1, part.dump(), now));
    store.expire(now + seconds(1)); // A new in-flight connection must survive a sampling tick.
    assert(store.frames({100, 10}, "chrome", 7).empty());
    part["partIndex"] = 1;
    part["frames"] = json::array({frame(4)});
    assert(store.receive(1, part.dump(), now + seconds(1)));
    assert(store.frames({100, 10}, "chrome", 7).size() == 2); // Commit only after every part.
    assert(!store.receive(1, part.dump(), now + seconds(2))); // Replay rejected.
    part["generation"] = 4;
    assert(!store.receive(1, part.dump(), now)); // Out-of-order first part.
    part["partIndex"] = 0;
    assert(store.receive(1, part.dump(), now + seconds(2)));
    part["partIndex"] = 1;
    assert(!store.receive(1, part.dump(), now + seconds(3))); // Duplicate tab/frame across parts.
    assert(store.frames({100, 10}, "chrome", 7).size() == 2); // Bad snapshot keeps last complete state.

    auto invalid = snapshot(json::array({frame()}), 5);
    for (const auto& key : {"browserPid", "generation", "partCount", "frames", "browserStartTime"}) {
        auto bad = invalid;
        bad[key] = false;
        assert(!store.receive(1, bad.dump(), now));
    }
    invalid["frames"][0]["requestCount"] = -1;
    assert(!store.receive(1, invalid.dump(), now));
    invalid["frames"][0] = frame();
    invalid["frames"][0]["url"] = "chrome://settings";
    assert(!store.receive(1, invalid.dump(), now));
    assert(!store.receive(1, R"({"type":"tab","browser":"chrome","rendererClientId":7})", now));

    assert(store.receive(4, snapshot(json::array({frame(8)}), 1).dump(), now + seconds(3)));
    assert(store.frames({100, 10}, "chrome", 7).size() == 1); // New connection supersedes old same-profile port.
    assert(!store.receive(1, snapshot(json::array({frame(9)}), 6).dump(), now + seconds(4)));
    store.disconnect(1);
    assert(store.frames({100, 10}, "chrome", 7)[0].tabId == 8);
    store.expire(now + seconds(19));
    assert(store.frames({100, 10}, "chrome", 7).empty());
    assert(store.frames({200, 20}, "chrome", 7).empty());

    Store chunked;
    part = snapshot(json::array({frame()})); part["partCount"] = 2;
    assert(chunked.receive(10, part.dump(), now));
    chunked.expire(now + seconds(1));
    part["partIndex"] = 1; part["frames"] = json::array({frame(2)});
    assert(chunked.receive(10, part.dump(), now + seconds(2)));
    assert(chunked.frames({100, 10}, "chrome", 7).size() == 2);
    chunked.retain({{100, 11}});
    assert(chunked.frames({100, 10}, "chrome", 7).empty());

    const std::unordered_map<pid_t, argus::ProcessRecord> records {
        {101, {101, 100, "helper"}}, {102, {102, 101, "renderer"}}, {201, {201, 200, "renderer"}},
        {999, {999, 999, "cycle"}}
    };
    const std::map<pid_t, argus::BrowserRoot> roots { {100, {{100, 10}, "chrome"}}, {200, {{200, 20}, "chrome"}} };
    assert(argus::browserAncestor(102, records, roots)->identity.pid == 100);
    assert(argus::browserAncestor(201, records, roots)->identity.pid == 200);
    assert(!argus::browserAncestor(999, records, roots));
    assert(argus::browserKind("Google Chrome for Testing") == "chrome");
    std::cout << "PASS: browser/profile isolation, shared renderers, atomic snapshots, cleanup, expiry, PID reuse, ancestry\n";
}
