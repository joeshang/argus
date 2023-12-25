//
//  main.cpp
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/11/25.
//

#include <iostream>
#include <cstdio>
#include <dispatch/dispatch.h>
#include <CoreFoundation/CFRunLoop.h>

#include "BrowserMonitor.hpp"
#include "SocketServer.hpp"
#include "TerminalDisplayer.hpp"

void signalCallbackHandler(int signum) {
    CFRunLoopStop(CFRunLoopGetCurrent());
}

int main(int argc, const char * argv[]) {
    signal(SIGINT, signalCallbackHandler);
    signal(SIGTERM, signalCallbackHandler);

    std::cout << "Argus Native Service\n";
    std::cout << "start working...\n\n";

    auto monitor = std::make_unique<BrowserMonitor>();
    auto server = std::make_unique<SocketServer>([&monitor](std::string &message) {
        monitor->receiveMessage(message);
    });
    server->setup();

    auto displayer = std::make_unique<TerminalDisplayer>();
    monitor->beginMetric(2, [&displayer, &monitor]() {
        displayer->showMetrics(monitor->getChromeProcessInfo(),
                               monitor->getEdgeProcessInfo(),
                               monitor->getSafariProcessInfo(),
                               monitor->getChromeWebMetric(),
                               monitor->getEdgeWebMetric(),
                               monitor->getSafariWebMetric());
    });

    displayer->loop();

    return 0;
}
