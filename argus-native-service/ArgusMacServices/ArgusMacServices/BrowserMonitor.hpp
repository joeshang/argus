//
//  ProcessMonitor.hpp
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/11/27.
//

#ifndef ProcessMonitor_hpp
#define ProcessMonitor_hpp

#include <libproc.h>
#include <cstdint>
#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <iomanip>

enum class ChromiumProcessType: int {
    Browser,
    Tab,
    Subframe,
    PrerenderTab,
    Extension,
    GPUProcess,
    NetworkService,
    StorageService,
    AudioService,
    AssetStoreService,
    DataDecoderService,
    V8ProxyResolver,
};

enum class SafariProcessType: int {
    Browser,
    Tab,
    GPU,
    Network,
};

std::string typeString(ChromiumProcessType processType);
std::string typeString(SafariProcessType processType);

struct UsageData {
    uint64_t cpuTime = 0;
    uint64_t memoryUsage = 0;
    float cpuPercent = 0;
};

struct WebMetricData {
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

struct ChromiumProcessInfo {
    pid_t pid;
    ChromiumProcessType type;
    int rendererClientId = 0;
    std::string url;
    UsageData usage;

    friend std::ostream &operator<<(std::ostream &stream,
                                    const ChromiumProcessInfo &info) {
        auto type = typeString(info.type);
        stream << std::fixed << std::setw(10) << info.pid << "\t"
        << std::fixed << std::setw(10) << std::setprecision(6) << info.usage.cpuPercent << "\t"
        << std::fixed << std::setw(10) << info.usage.memoryUsage / 1024 / 1024 << "\t"
        << std::fixed << std::setw(20) << type << "\t"
        << std::fixed << std::setw(10) << info.rendererClientId << "\t"
        << info.url << "\n";
        return stream;
    }
};

struct SafariProcessInfo {
    pid_t pid;
    SafariProcessType type;
    std::string url;
    UsageData usage;

    friend std::ostream &operator<<(std::ostream &stream,
                                    const SafariProcessInfo &info) {
        auto type = typeString(info.type);
        stream << std::fixed << std::setw(10) << info.pid << "\t"
        << std::fixed << std::setw(10) << std::setprecision(6) << info.usage.cpuPercent << "\t"
        << std::fixed << std::setw(10) << info.usage.memoryUsage / 1024 / 1024 << "\t"
        << std::fixed << std::setw(20) << type << "\t"
        << info.url << "\n";
        return stream;
    }
};


class BrowserMonitor {
public:
    void beginMetric(int interval,
                     std::function<void(void)> callback);
    void receiveMessage(std::string &message);
    void printUsage();

    std::vector<ChromiumProcessInfo> getChromeProcessInfo() { return m_chromeProcesses; }
    std::vector<ChromiumProcessInfo> getEdgeProcessInfo() { return m_edgeProcesses; }
    std::vector<SafariProcessInfo> getSafariProcessInfo() { return m_safariProcesses; }
    std::vector<WebMetricData> getChromeWebMetric() {
        std::vector<WebMetricData> result {};
        for (const auto& item: m_chromeProcesses) {
            if (item.type != ChromiumProcessType::Tab) {
                continue;
            }
            if (!m_chromeTabInfos.contains(item.rendererClientId)) {
                continue;
            }
            auto metric = m_chromeTabInfos[item.rendererClientId];
            if (!metric.withMetric) {
                continue;
            }
            result.push_back(metric);
        }
        return result;
    }
    std::vector<WebMetricData> getEdgeWebMetric() {
        std::vector<WebMetricData> result {};
        for (const auto& item: m_edgeProcesses) {
            if (item.type != ChromiumProcessType::Tab) {
                continue;
            }
            if (!m_edgeTabInfos.contains(item.rendererClientId)) {
                continue;
            }
            auto metric = m_edgeTabInfos[item.rendererClientId];
            if (!metric.withMetric) {
                continue;
            }
            result.push_back(metric);
        }
        return result;
    }
    std::vector<WebMetricData> getSafariWebMetric() {
        std::vector<WebMetricData> result {};
        // TODO:
        return result;
    }

private:
    void queryBrowserProcesses();
    void fetchBrowserProcessUsage();
    void handleChromiumProcessArguments(ChromiumProcessInfo &info);
    UsageData getProcessUsage(pid_t pid);

    int m_sampleInterval = 5;
    std::function<void(void)> m_metricCallback = nullptr;
    std::vector<ChromiumProcessInfo> m_chromeProcesses;
    std::vector<ChromiumProcessInfo> m_edgeProcesses;
    std::vector<SafariProcessInfo> m_safariProcesses;
    std::unordered_map<pid_t, UsageData> m_prevUsage;
    std::unordered_map<int, WebMetricData> m_chromeTabInfos;
    std::unordered_map<int, WebMetricData> m_edgeTabInfos;
    std::unordered_map<int, WebMetricData> m_safariTabInfos;
};

#endif /* ProcessMonitor_hpp */
