//
//  ProcessMonitor.cpp
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/11/27.
//

// 1. list all pids
// 2. get process name by pid
// 3. filter Chrome and Safari subprocess and get its performance
// 4. Chrome
//    * analysis Chrome's subprocess launch argument
//    * fetch rendererClientId for arguments
//    * get url of subprocess via renderClientId
// 5. Safari
//    * get host url of subprocess via private api

#include "BrowserMonitor.hpp"
#include "ProcessUtils.hpp"

#include <libproc.h>
#include <mach/mach_time.h>
#include <dispatch/dispatch.h>
#include <vector>
#include <string>
#include <sstream>
#include <nlohmann/json.hpp>

extern "C" {
extern pid_t responsibility_get_pid_responsible_for_pid(pid_t);
}

constexpr static const char *s_chromeProcess = "Google Chrome";
constexpr static const char *s_chromeSubprocessPrefix = "Google Chrome Helper";

constexpr static const char *s_edgeProcess = "Microsoft Edge";
constexpr static const char *s_edgeSubrocessPrefix = "Microsoft Edge Helper";

constexpr static const char *s_safariProcess = "Safari";
constexpr static const char *s_webkitWebContentProcess = "com.apple.WebKit.WebContent";
constexpr static const char *s_webkitGPUProcess = "com.apple.WebKit.GPU";
constexpr static const char *s_webkitNetworkProcess = "com.apple.WebKit.Networking";


WebMetricData decodeMessage(nlohmann::json &json) {
    WebMetricData data {};
    if (json.contains("url")) {
        data.url = json["url"].get<std::string>();
    }
    if (json.contains("isSubframe")) {
        data.isSubframe = json["isSubframe"].get<bool>();
    }
    if (json.contains("withMetric")) {
        data.withMetric = json["withMetric"].get<bool>();
    }
    if (json.contains("webRequestDurationMs")) {
        data.webRequestDurationMs = json["webRequestDurationMs"].get<int>();
    }
    if (json.contains("pageLoadTotalDurationMs")) {
        data.pageLoadTotalDurationMs = json["pageLoadTotalDurationMs"].get<int>();
    }
    if (json.contains("pageLoadNetworkDurationMs")) {
        data.pageLoadNetworkDurationMs = json["pageLoadNetworkDurationMs"].get<int>();
    }
    if (json.contains("pageLoadRenderDurationMs")) {
        data.pageLoadRenderDurationMs = json["pageLoadRenderDurationMs"].get<int>();
    }
    if (json.contains("requestCount")) {
        data.requestCount = json["requestCount"].get<int>();
    }
    if (json.contains("httpStatusCount2xx")) {
        data.httpStatusCount2xx = json["httpStatusCount2xx"].get<int>();
    }
    if (json.contains("httpStatusCount3xx")) {
        data.httpStatusCount3xx = json["httpStatusCount3xx"].get<int>();
    }
    if (json.contains("httpStatusCount4xx")) {
        data.httpStatusCount4xx = json["httpStatusCount4xx"].get<int>();
    }
    if (json.contains("httpStatusCount5xx")) {
        data.httpStatusCount5xx = json["httpStatusCount5xx"].get<int>();
    }
    return data;
}

void BrowserMonitor::beginMetric(int interval,
                                 std::function<void(void)> callback) {
    m_sampleInterval = interval;
    m_metricCallback = callback;

    dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
    dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    dispatch_set_context(timer, this);
    dispatch_source_set_event_handler_f(timer, [](void *data) {
        auto m = static_cast<BrowserMonitor *>(data);
        m->queryBrowserProcesses();
        m->fetchBrowserProcessUsage();
        m->m_metricCallback();
    });
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 0), interval * NSEC_PER_SEC, 0 * NSEC_PER_SEC);
    dispatch_resume(timer);
}

void BrowserMonitor::receiveMessage(std::string &message) {
    auto json = nlohmann::json::parse(message, nullptr, false);
    if (json.is_discarded()) {
        return;
    }
    if (!json.contains("type")) {
        return;
    }
    if (!json.contains("browser")) {
        return;
    }
    auto type = json["type"].get<std::string>();
    auto browser = json["browser"].get<std::string>();
    if (type == "tab") {
        auto rendererClientId = json["rendererClientId"].get<int>();
        auto info = decodeMessage(json);
        if (browser == "chrome") {
            m_chromeTabInfos[rendererClientId] = info;
        } else if (browser == "edge") {
            m_edgeTabInfos[rendererClientId] = info;
        }
    }
}

void BrowserMonitor::queryBrowserProcesses() {
    int pid_size_needed = proc_listallpids(nullptr, 0);
    if (pid_size_needed <= 0) {
        return;
    }

    std::vector<pid_t> pids(pid_size_needed);
    int pid_count = proc_listallpids(pids.data(), int(pids.size() * sizeof(pid_t)));
    if (pid_count <= 0) {
        return;
    }

    pids.resize(pid_count);
    sort(pids.begin(), pids.end());

    std::vector<ChromiumProcessInfo> chrome {};
    std::vector<ChromiumProcessInfo> edge {};
    std::vector<SafariProcessInfo> safari {};

    pid_t chromeId = 0;
    pid_t edgeId = 0;
    pid_t safariId = 0;
    for (auto &pid: pids) {
        char buffer[MAXPATHLEN];
        proc_name(pid, buffer, sizeof(buffer));
        std::string name = buffer;
        if (name == s_chromeProcess) {
            chromeId = pid;
            ChromiumProcessInfo info;
            info.pid = pid;
            info.type = ChromiumProcessType::Browser;
            chrome.push_back(info);
        } else if (name == s_edgeProcess) {
            edgeId = pid;
            ChromiumProcessInfo info;
            info.pid = pid;
            info.type = ChromiumProcessType::Browser;
            edge.push_back(info);
        } else if (name == s_safariProcess) {
            safariId = pid;
            SafariProcessInfo info;
            info.pid = pid;
            info.type = SafariProcessType::Browser;
            safari.push_back(info);
        }
    }

    for (auto &pid: pids) {
        char buffer[MAXPATHLEN];
        proc_name(pid, buffer, sizeof(buffer));
        std::string name = buffer;

        // Other apps which use WebKit will have the same name of com.apple.WebKit.WebContent
        // and we can't use ppid to identify which one is belong to Safari, because all WebContent's ppid is 1 (launchd)
        // Actually, on macos we need use private api `responsibility_get_pid_responsible_for_pid` to fetch real "ppid" (with root)
        // https://apple.stackexchange.com/questions/327290/how-process-hierarchy-works-in-macos
        pid_t ppid = responsibility_get_pid_responsible_for_pid(pid);

        if (name.starts_with(s_chromeSubprocessPrefix) && ppid == chromeId) {
            ChromiumProcessInfo info;
            info.pid = pid;
            handleChromiumProcessArguments(info);
            if (m_chromeTabInfos.contains(info.rendererClientId)) {
                auto tabInfo = m_chromeTabInfos[info.rendererClientId];
                info.url = tabInfo.url;
                if (tabInfo.isSubframe) {
                    info.type = ChromiumProcessType::Subframe;
                }
            }
            chrome.push_back(info);
        } else if (name.starts_with(s_edgeSubrocessPrefix) && ppid == edgeId) {
            ChromiumProcessInfo info;
            info.pid = pid;
            handleChromiumProcessArguments(info);
            if (m_edgeTabInfos.contains(info.rendererClientId)) {
                auto tabInfo = m_edgeTabInfos[info.rendererClientId];
                info.url = tabInfo.url;
                if (tabInfo.isSubframe) {
                    info.type = ChromiumProcessType::Subframe;
                }
            }
            edge.push_back(info);
        } else if (ppid == safariId) {
            if (name == s_webkitWebContentProcess) {
                SafariProcessInfo info;
                info.pid = pid;
                info.type = SafariProcessType::Tab;
                info.url = processUtilsGetWebKitActiveUrl(pid);
                safari.push_back(info);
            } else if (name == s_webkitGPUProcess) {
                SafariProcessInfo info;
                info.pid = pid;
                info.type = SafariProcessType::GPU;
                safari.push_back(info);
            } else if (name == s_webkitNetworkProcess) {
                SafariProcessInfo info;
                info.pid = pid;
                info.type = SafariProcessType::Network;
                safari.push_back(info);
            }
        }
    }

    m_chromeProcesses = chrome;
    m_edgeProcesses = edge;
    m_safariProcesses = safari;
}

void BrowserMonitor::fetchBrowserProcessUsage() {
    std::unordered_map<pid_t, UsageData> currentUsage {};

    for (auto &info: m_chromeProcesses) {
        info.usage = getProcessUsage(info.pid);
        currentUsage[info.pid] = info.usage;
    }

    for (auto &info: m_edgeProcesses) {
        info.usage = getProcessUsage(info.pid);
        currentUsage[info.pid] = info.usage;
    }

    for (auto &info: m_safariProcesses) {
        info.usage = getProcessUsage(info.pid);
        currentUsage[info.pid] = info.usage;
    }

    m_prevUsage = currentUsage;
}

void BrowserMonitor::printUsage() {
    std::cout << "============ Chrome ===========\n"
    << std::fixed << std::setw(10) << "pid" << "\t"
    << std::fixed << std::setw(10) << "cpu" << "\t"
    << std::fixed << std::setw(10) << "mem" << "\t"
    << std::fixed << std::setw(10) << "type" << "\t"
    << std::fixed << std::setw(20) << "renderId" << "\t"
    << "url" << "\n";
    for (const auto &info: m_chromeProcesses) {
        std::cout << info;
    }
    std::cout << "\n";

    std::cout << "============ Edge ===========\n"
    << std::fixed << std::setw(10) << "pid" << "\t"
    << std::fixed << std::setw(10) << "cpu" << "\t"
    << std::fixed << std::setw(10) << "mem" << "\t"
    << std::fixed << std::setw(10) << "type" << "\t"
    << std::fixed << std::setw(20) << "renderId" << "\t"
    << "url" << "\n";
    for (const auto &info: m_edgeProcesses) {
        std::cout << info;
    }
    std::cout << "\n";

    std::cout << "============ Safari ===========\n"
    << std::fixed << std::setw(10) << "pid" << "\t"
    << std::fixed << std::setw(10) << "cpu" << "\t"
    << std::fixed << std::setw(10) << "mem" << "\t"
    << std::fixed << std::setw(20) << "type" << "\t"
    << std::fixed << "url" << "\n";
    for (const auto &info: m_safariProcesses) {
        std::cout << info;
    }

    std::cout << "\n\n";
}

UsageData BrowserMonitor::getProcessUsage(pid_t pid) {
    UsageData data {};
    struct rusage_info_v2 rusage_info_data;
    proc_pid_rusage(pid, RUSAGE_INFO_V2, (rusage_info_t *)&rusage_info_data);
    data.memoryUsage = rusage_info_data.ri_phys_footprint;
    static mach_timebase_info_data_t timebase;
    if (timebase.denom == 0) {
      mach_timebase_info(&timebase);
    }
    data.cpuTime = (rusage_info_data.ri_user_time + rusage_info_data.ri_system_time) * timebase.numer / timebase.denom;
    uint64_t prevCpuTime = 0;
    if (m_prevUsage.contains(pid)) {
        auto prev = m_prevUsage[pid];
        prevCpuTime = prev.cpuTime;
        data.cpuPercent = float(data.cpuTime - prevCpuTime) / (float(m_sampleInterval) * 1000000000);
    }
    return data;
}

void BrowserMonitor::handleChromiumProcessArguments(ChromiumProcessInfo &info) {
    std::vector<std::string> arguments;
    processUtilsGetProcessArguments(info.pid, &arguments);

    bool isExtension = false;
    bool isPrerender = false;
    for (auto &argument: arguments) {
        if (argument == "--type=renderer") {
            info.type = ChromiumProcessType::Tab;
        } else if (argument == "--type=gpu-process") {
            info.type = ChromiumProcessType::GPUProcess;
        } else if (argument == "--utility-sub-type=network.mojom.NetworkService") {
            info.type = ChromiumProcessType::NetworkService;
        } else if (argument == "--utility-sub-type=storage.mojom.StorageService") {
            info.type = ChromiumProcessType::StorageService;
        } else if (argument == "--utility-sub-type=audio.mojom.AudioService") {
            info.type = ChromiumProcessType::AudioService;
        } else if (argument == "--utility-sub-type=asset_store.mojom.AssetStoreService") {
            info.type = ChromiumProcessType::AssetStoreService;
        } else if (argument == "--utility-sub-type=data_decoder.mojom.DataDecoderService") {
            info.type = ChromiumProcessType::DataDecoderService;
        } else if (argument == "--utility-sub-type=proxy_resolver.mojom.ProxyResolverFactory") {
            info.type = ChromiumProcessType::V8ProxyResolver;
        } else if (argument == "--extension-process") {
            isExtension = true;
        } else if (argument == "--instant-process") {
            // Edge's prerender tab will have this argument
            isPrerender = true;
        } else if (argument.starts_with("--renderer-client-id=")) {
            std::string prefix = "--renderer-client-id=";
            std::string clientId = argument.substr(prefix.size(), argument.size() - prefix.size());
            info.rendererClientId = std::stoi(clientId);
        }
    }

    if (info.type == ChromiumProcessType::Tab) {
        if (isExtension) {
            info.type = ChromiumProcessType::Extension;
        } else if (isPrerender) {
            info.type = ChromiumProcessType::PrerenderTab;
        }
    }
}


std::string typeString(ChromiumProcessType processType) {
    std::string type {};
    switch (processType) {
        case ChromiumProcessType::Browser:
            type = "Browser";
            break;
        case ChromiumProcessType::Tab:
            type = "Tab";
            break;
        case ChromiumProcessType::Subframe:
            type = "Subframe";
            break;
        case ChromiumProcessType::PrerenderTab:
            type = "Prerender Tab";
            break;
        case ChromiumProcessType::Extension:
            type = "Extension";
            break;
        case ChromiumProcessType::GPUProcess:
            type = "GPU Process";
            break;
        case ChromiumProcessType::AudioService:
            type = "Audio Service";
            break;
        case ChromiumProcessType::NetworkService:
            type = "Network Service";
            break;
        case ChromiumProcessType::StorageService:
            type = "Storage Service";
            break;
        case ChromiumProcessType::AssetStoreService:
            type = "Asset Store Service";
            break;
        case ChromiumProcessType::DataDecoderService:
            type = "Data Decoder Service";
            break;
        case ChromiumProcessType::V8ProxyResolver:
            type = "V8 Proxy Resolver";
            break;
        default:
            break;
    }
    return type;
}

std::string typeString(SafariProcessType processType) {
    std::string type {};
    switch (processType) {
        case SafariProcessType::Browser:
            type = "Browser";
            break;
        case SafariProcessType::Tab:
            type = "Tab";
            break;
        case SafariProcessType::GPU:
            type = "GPU";
            break;
        case SafariProcessType::Network:
            type = "Network";
            break;
        default:
            break;
    }
    return type;
}
