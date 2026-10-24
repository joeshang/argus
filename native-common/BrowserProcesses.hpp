#pragma once

#include "ProcessUsage.hpp"
#include "TabSnapshot.hpp"
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/proc_info.h>

namespace argus {

struct ProcessRecord { pid_t pid = 0, parent = 0; std::string name; };
struct BrowserRoot { BrowserIdentity identity; std::string kind; };

inline std::string browserKind(const std::string& name) {
    if (name == "Google Chrome" || name == "Google Chrome Canary" || name == "Google Chrome Beta" ||
        name == "Google Chrome Dev" || name == "Google Chrome for Testing" || name == "Chromium") return "chrome";
    if (name == "Microsoft Edge" || name == "Microsoft Edge Beta" || name == "Microsoft Edge Dev" ||
        name == "Microsoft Edge Canary") return "edge";
    return name == "Safari" ? "safari" : "";
}

inline std::optional<BrowserRoot> browserAncestor(pid_t pid, const std::unordered_map<pid_t, ProcessRecord>& records,
                                                 const std::map<pid_t, BrowserRoot>& roots) {
    for (int depth = 0; depth < 64 && pid > 0; ++depth) {
        if (auto found = roots.find(pid); found != roots.end()) return found->second;
        const auto found = records.find(pid);
        if (found == records.end() || found->second.parent == pid) break;
        pid = found->second.parent;
    }
    return std::nullopt;
}

inline std::unordered_map<pid_t, ProcessRecord> readProcessRecords() {
    const int estimate = proc_listallpids(nullptr, 0);
    if (estimate <= 0) return {};
    std::vector<pid_t> pids(static_cast<size_t>(estimate) + 64);
    const int count = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    if (count <= 0 || static_cast<size_t>(count) > pids.size()) return {};
    std::unordered_map<pid_t, ProcessRecord> result;
    for (int i = 0; i < count; ++i) {
        proc_bsdinfo info {};
        if (proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info)) continue;
        char path[PROC_PIDPATHINFO_MAXSIZE] {};
        std::string name;
        if (proc_pidpath(pids[i], path, sizeof(path)) > 0) {
            const std::string full(path);
            name = full.substr(full.find_last_of('/') + 1);
        } else name.assign(info.pbi_name, strnlen(info.pbi_name, sizeof(info.pbi_name)));
        result.emplace(pids[i], ProcessRecord { pids[i], static_cast<pid_t>(info.pbi_ppid), std::move(name) });
    }
    return result;
}

} // namespace argus
