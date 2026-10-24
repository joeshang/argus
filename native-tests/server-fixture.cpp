#include "../argus-native-service/ArgusMacServices/ArgusMacServices/SocketServer.hpp"
#include "../argus-native-service/ArgusMacServices/ArgusMacServices/BrowserMonitor.hpp"
#include <iostream>
#include <sys/stat.h>

int main() {
    BrowserMonitor monitor;
    SocketServer server([&](const std::string& message) {
        monitor.receiveMessage(message); // Exercise the production JSON consumer too.
        std::cout << "MESSAGE " << message << std::endl;
    });
    const auto original = umask(0027);
    const bool ready = server.setup();
    const auto after = umask(original);
    if (after != 0027) return 99;
    if (!ready) return 1;
    std::cout << "READY" << std::endl;
    std::string command;
    std::getline(std::cin, command);
}
