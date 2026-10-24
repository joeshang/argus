//
//  TerminalDisplayer.hpp
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/12/4.
//

#ifndef TerminalDisplayer_hpp
#define TerminalDisplayer_hpp

#include <unordered_map>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/screen.hpp>

#include "BrowserMonitor.hpp"

using namespace ftxui;

struct PerfTableItem {
    std::string processId;
    std::string type;
    float cpuPercent;
    float memory;
    std::string url;

    PerfTableItem(const ChromiumProcessInfo &info) {
        processId = std::to_string(info.pid);
        type = typeString(info.type);
        cpuPercent = info.usage.cpuPercent;
        memory = info.usage.memoryUsage;
        url = info.url;
    }

    PerfTableItem(const SafariProcessInfo &info) {
        processId = std::to_string(info.pid);
        type = typeString(info.type);
        cpuPercent = info.usage.cpuPercent;
        memory = info.usage.memoryUsage;
        url = info.url;
    }
};

class TerminalDisplayer {
public:
    TerminalDisplayer():m_screen(ScreenInteractive::Fullscreen()) {}

    void loop();
    void showMetrics(BrowserSnapshot snapshot);

private:
    ScreenInteractive m_screen;
    std::unordered_map<std::string, std::vector<PerfTableItem>> m_perfTableData;
    std::unordered_map<std::string, std::vector<WebMetricData>> m_webMetricTableData;
};

#endif /* TerminalDisplayer_hpp */
