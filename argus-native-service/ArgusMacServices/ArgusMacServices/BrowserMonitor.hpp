//
//  ProcessMonitor.hpp
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/11/27.
//

#ifndef ProcessMonitor_hpp
#define ProcessMonitor_hpp

#include <libproc.h>
#include "../../../native-common/ProcessUsage.hpp"
#include "../../../native-common/TabSnapshot.hpp"
#include <cstdint>
#include <string>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
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
    Other,
};

enum class SafariProcessType: int {
    Browser,
    Tab,
    GPU,
    Network,
};

std::string typeString(ChromiumProcessType processType);
std::string typeString(SafariProcessType processType);

using UsageData = argus::UsageData;

using WebMetricData = argus::FrameData;

struct ChromiumProcessInfo {
    pid_t pid = 0;
    ChromiumProcessType type = ChromiumProcessType::Other;
    argus::BrowserIdentity browser;
    std::vector<argus::FrameData> frames;
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


struct BrowserSnapshot {
    std::vector<ChromiumProcessInfo> chrome;
    std::vector<ChromiumProcessInfo> edge;
    std::vector<SafariProcessInfo> safari;
    std::vector<WebMetricData> chromeWebMetric;
    std::vector<WebMetricData> edgeWebMetric;
    std::vector<WebMetricData> safariWebMetric;
};

class BrowserMonitor {
public:
    ~BrowserMonitor();
    // Start/stop are called by the owning thread, never from the sample callback.
    void beginMetric(int interval, std::function<void(BrowserSnapshot)> callback);
    void stopMetric();
    void receiveMessage(uint64_t connectionId, const std::string& message);
    void disconnect(uint64_t connectionId);
    BrowserSnapshot snapshot() const;
    void printUsage();

private:
    BrowserSnapshot snapshotLocked() const;
    void queryBrowserProcesses();
    void fetchBrowserProcessUsage();
    void handleChromiumProcessArguments(ChromiumProcessInfo &info);
    UsageData getProcessUsage(pid_t pid);

    mutable std::mutex m_mutex;
    std::condition_variable m_wakeup;
    std::thread m_sampler;
    bool m_stopping = true;
    std::vector<ChromiumProcessInfo> m_chromeProcesses;
    std::vector<ChromiumProcessInfo> m_edgeProcesses;
    std::vector<SafariProcessInfo> m_safariProcesses;
    argus::ProcessUsageTracker m_usage;
    argus::TabSnapshots m_tabs;
};

#endif /* ProcessMonitor_hpp */
