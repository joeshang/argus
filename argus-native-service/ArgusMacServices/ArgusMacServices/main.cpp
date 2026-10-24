#include <iostream>

#include "BrowserMonitor.hpp"
#include "SocketServer.hpp"
#include "TerminalDisplayer.hpp"

int main() {
    std::cout << "Argus Native Service\nstart working...\n\n";
    // Destruction order: stop socket callbacks, join the sampler, then destroy
    // the UI. No producer can post into a destroyed screen or monitor.
    TerminalDisplayer displayer;
    BrowserMonitor monitor;
    SocketServer server([&monitor](const std::string& message) {
        monitor.receiveMessage(message);
    });
    if (!server.setup()) return EXIT_FAILURE;
    monitor.beginMetric(2, [&displayer](BrowserSnapshot snapshot) {
        displayer.showMetrics(std::move(snapshot));
    });
    displayer.loop();
    monitor.stopMetric();
    return EXIT_SUCCESS;
}
