#include "../argus-native-service/ArgusMacServices/ArgusMacServices/SocketServer.hpp"
#include "../argus-native-service/ArgusMacServices/ArgusMacServices/BrowserMonitor.hpp"
#include <iostream>
#include <mutex>
#include <sys/stat.h>

int main(int argc, char**) {
    std::mutex output;
    BrowserMonitor monitor;
    SocketServer server([&](uint64_t id, const std::string& message) {
        monitor.receiveMessage(id, message); // Exercise the production JSON consumer too.
        std::lock_guard print(output);
        std::cout << "MESSAGE " << message << std::endl;
    }, [&](uint64_t id) { monitor.disconnect(id); });
    const auto original = umask(0027);
    const bool ready = server.setup();
    const auto after = umask(original);
    if (after != 0027) return 99;
    if (!ready) return 1;
    std::cout << "READY" << std::endl;
    if (argc > 1) monitor.beginMetric(1, [&](BrowserSnapshot snapshot) {
        nlohmann::json processes = nlohmann::json::array();
        for (const auto& process : snapshot.chrome) {
            nlohmann::json frames = nlohmann::json::array();
            for (const auto& frame : process.frames) frames.push_back({{"source", frame.sourceId}, {"tab", frame.tabId}, {"url", frame.url}});
            processes.push_back({{"pid", process.pid}, {"browserPid", process.browser.pid}, {"frames", frames}});
        }
        std::lock_guard print(output);
        std::cout << "SAMPLE " << processes.dump() << std::endl;
    });
    std::string command;
    std::getline(std::cin, command);
}
