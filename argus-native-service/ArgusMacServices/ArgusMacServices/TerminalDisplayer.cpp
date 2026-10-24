//
//  TerminalDisplayer.cpp
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/12/4.
//

#include "TerminalDisplayer.hpp"
#include <sstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color_info.hpp>
#include <ftxui/screen/terminal.hpp>

void TerminalDisplayer::showMetrics(BrowserSnapshot snapshot) {
    // Post owns the snapshot. Table state is only touched by the UI loop,
    // including updates; posting a mere redraw event would leave a data race.
    m_screen.Post(Closure([this, data = std::move(snapshot)]() mutable {
        auto table = [](const auto& processes) {
            std::vector<PerfTableItem> result;
            for (const auto& info : processes) result.emplace_back(info);
            return result;
        };
        m_perfTableData["chrome"] = table(data.chrome);
        m_perfTableData["edge"] = table(data.edge);
        m_perfTableData["safari"] = table(data.safari);
        m_webMetricTableData["chrome"] = std::move(data.chromeWebMetric);
        m_webMetricTableData["edge"] = std::move(data.edgeWebMetric);
        m_webMetricTableData["safari"] = std::move(data.safariWebMetric);
        m_screen.Post(Event::Custom);
    }));
}

void TerminalDisplayer::loop() {
    std::vector<std::string> browser_names = {
        "chrome",
        "edge",
        "safari",
    };

    std::vector<Component> renders {};
    for (const auto &name: browser_names) {
        auto render = Renderer([this, name] {
            std::string urlTitle = "Url";
            int urlMaxLength = 150;
            for (int i = 0; i < urlMaxLength; i++) {
                urlTitle += " ";
            }
            std::vector<std::string> titles = {"Process ID", "Browser PID", "Type", "CPU", "Memory Footprint", urlTitle};
            std::vector<std::vector<std::string>> data = { titles };
            std::vector<int> alerts {};
            int index = 1;
            for (const auto &info: m_perfTableData[name]) {
                std::vector<std::string> item {};
                item.push_back(info.processId);
                item.push_back(info.browserId);
                item.push_back(info.type);
                std::stringstream cpu {};
                double cpuUsage = info.cpuPercent * 100;
                cpu << std::fixed << std::setprecision(1) << cpuUsage;
                item.push_back(info.cpuAvailable ? cpu.str() : "—");
                std::stringstream mem {};
                double memUsage = double(info.memory) / 1024 / 1024;
                mem << std::fixed << std::setprecision(1) << memUsage << " MB";
                item.push_back(info.available ? mem.str() : "—");
                auto url = info.url;
                if (url.length() > urlMaxLength) {
                    url = url.substr(0, urlMaxLength - 3) + "...";
                }
                item.push_back(url);
                data.push_back(item);

                if (info.type == "Tab") {
                    if (info.cpuAvailable && cpuUsage > 30) {
                        alerts.push_back(index);
                    } else if (info.available && memUsage > 200) {
                        alerts.push_back(index);
                    }
                }

                index++;
            }

            auto table = Table(data);

            table.SelectAll().Border(LIGHT);

            // Make first row bold with a double border.
            table.SelectRow(0).Decorate(bold);
            table.SelectRow(0).SeparatorVertical(LIGHT);
            table.SelectRow(0).Border(DOUBLE);

            for (int i = 0; i < titles.size(); i++) {
                table.SelectColumn(i).Border(LIGHT);
            }

            for (const auto &index: alerts) {
                table.SelectRow(index).Decorate(color(Color::Red));
            }
            return table.Render();
        });
        renders.push_back(render);
    }

    for (const auto &name: browser_names) {
        auto render = Renderer([this, name] {
            std::string urlTitle = "Url";
            int urlMaxLength = 130;
            for (int i = 0; i < urlMaxLength; i++) {
                urlTitle += " ";
            }
            std::vector<std::string> titles = {
                "Browser PID", "Source", "Tab ID", "Document",
                "WebRequest",
                "PageLoadTotal",
                "PageLoadNetwork",
                "PageLoadRender",
                "RequestCount",
                "2xxCount",
                "3xxCount",
                "4xxCount",
                "5xxCount",
                urlTitle
            };
            std::vector<std::vector<std::string>> data = { titles };
            std::vector<int> alerts {};
            for (const auto &info: m_webMetricTableData[name]) {
                std::vector<std::string> item {};
                item.push_back(std::to_string(info.browser.pid));
                item.push_back(info.sourceId.substr(0, 8));
                item.push_back(std::to_string(info.tabId));
                item.push_back(info.documentId.substr(0, 8));
                item.push_back(std::to_string(info.webRequestDurationMs));
                item.push_back(std::to_string(info.pageLoadTotalDurationMs));
                item.push_back(std::to_string(info.pageLoadNetworkDurationMs));
                item.push_back(std::to_string(info.pageLoadRenderDurationMs));
                item.push_back(std::to_string(info.requestCount));
                item.push_back(std::to_string(info.httpStatusCount2xx));
                item.push_back(std::to_string(info.httpStatusCount3xx));
                item.push_back(std::to_string(info.httpStatusCount4xx));
                item.push_back(std::to_string(info.httpStatusCount5xx));
                auto url = info.url;
                if (url.length() > urlMaxLength) {
                    url = url.substr(0, urlMaxLength - 3) + "...";
                }
                item.push_back(url);
                data.push_back(item);

            }

            auto table = Table(data);

            table.SelectAll().Border(LIGHT);

            // Make first row bold with a double border.
            table.SelectRow(0).Decorate(bold);
            table.SelectRow(0).SeparatorVertical(LIGHT);
            table.SelectRow(0).Border(DOUBLE);

            for (int i = 0; i < titles.size(); i++) {
                table.SelectColumn(i).Border(LIGHT);
            }

            for (const auto &index: alerts) {
                table.SelectRow(index).Decorate(color(Color::Red));
            }
            return table.Render();
        });
        renders.push_back(render);
    }

    std::vector<std::string> tab_entries {};
    for (const auto &name: browser_names) {
        tab_entries.push_back(name + "-perf");
    }
    for (const auto &name: browser_names) {
        tab_entries.push_back(name + "-pageload");
    }
    int tab_index = 0;
    auto tab_selection = Menu(&tab_entries, &tab_index, MenuOption::HorizontalAnimated());
    auto tab_content = Container::Tab(renders, &tab_index);

    auto main_container = Container::Vertical({
        tab_selection,
        tab_content,
    });

    auto main_renderer = Renderer(main_container, [&] {
        return vbox({
            text("Argus") | bold | hcenter,
            tab_selection->Render(),
            tab_content->Render() | flex,
        });
    });
    m_screen.Loop(main_renderer);
}
