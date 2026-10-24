#include "../argus-native-service/ArgusMacServices/ArgusMacServices/BrowserMonitor.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>

int main() {
    BrowserMonitor monitor;
    std::atomic<int> callbacks { 0 };
    monitor.beginMetric(1, [&](BrowserSnapshot data) {
        auto other = monitor.snapshot(); // Callback must run outside the state lock.
        ++callbacks;
    });
    std::vector<std::thread> writers;
    for (int worker = 0; worker < 4; ++worker) {
        writers.emplace_back([&, worker] {
            for (int i = 0; i < 10000; ++i) {
                const auto message = nlohmann::json {
                    {"type", "snapshot"}, {"protocolVersion", 2}, {"browser", "chrome"},
                    {"browserPid", getpid()}, {"browserStartTime", "1"}, {"sourceId", "worker-" + std::to_string(worker)},
                    {"generation", i + 1}, {"partIndex", 0}, {"partCount", 1}, {"frames", nlohmann::json::array()}
                };
                monitor.receiveMessage(worker + 1, message.dump());
            }
        });
    }
    std::thread reader([&] {
        for (int i = 0; i < 5000; ++i) { const auto data = monitor.snapshot(); }
    });
    for (auto& writer : writers) writer.join();
    reader.join();
    // Malformed JSON/schema must not escape as a conversion exception.
    for (const auto* message : { "garbage", "[]", "null", "{\"type\":\"tab\",\"browser\":true}",
            "{\"type\":\"tab\",\"browser\":\"chrome\",\"rendererClientId\":1,\"requestCount\":\"bad\"}",
            "{\"type\":\"tab\",\"browser\":\"chrome\",\"rendererClientId\":18446744073709551615}" }) {
        monitor.receiveMessage(1, message);
    }
    monitor.stopMetric();
    assert(callbacks > 0);
    // Repeated start/stop and destruction join every sampler; none outlives its owner.
    for (int i = 0; i < 20; ++i) {
        BrowserMonitor temporary;
        temporary.beginMetric(1, [](BrowserSnapshot) {});
    }
    std::cout << "PASS: concurrent messages, snapshots, sampler lifecycle, schema validation\n";
}
