#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <libproc.h>
#include <mach/mach_time.h>
#include <sys/resource.h>

namespace argus {

struct ProcessSample {
    uint64_t startTime = 0; // Process identity: ri_proc_start_abstime, not a duration.
    uint64_t cpuTimeNs = 0;
    uint64_t physicalBytes = 0;
};

inline std::optional<uint64_t> ticksToNanoseconds(uint64_t ticks, uint32_t numer, uint32_t denom) {
    if (denom == 0) return std::nullopt;
    const auto value = static_cast<unsigned __int128>(ticks) * numer / denom;
    if (value > UINT64_MAX) return std::nullopt;
    return static_cast<uint64_t>(value);
}

inline std::optional<ProcessSample> readProcessSample(pid_t pid) {
    rusage_info_v2 raw {};
    if (proc_pid_rusage(pid, RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&raw)) != 0 ||
        raw.ri_proc_start_abstime == 0 || raw.ri_user_time > UINT64_MAX - raw.ri_system_time) return std::nullopt;
    // XNU fill_task_rusage copies TASK_POWER_INFO's Mach counters unchanged.
    // Convert with a wide intermediate; keep startTime as opaque identity ticks.
    static const auto timebase = [] {
        mach_timebase_info_data_t value {};
        mach_timebase_info(&value);
        return value;
    }();
    const auto nanoseconds = ticksToNanoseconds(raw.ri_user_time + raw.ri_system_time, timebase.numer, timebase.denom);
    if (!nanoseconds) return std::nullopt;
    return ProcessSample { raw.ri_proc_start_abstime, *nanoseconds, raw.ri_phys_footprint };
}

struct UsageData {
    uint64_t cpuTime = 0;
    uint64_t memoryUsage = 0;
    double cpuPercent = 0; // Fraction of one logical CPU; may exceed 1.
    bool available = false;
    bool cpuAvailable = false;
};

class ProcessUsageTracker {
public:
    using Clock = std::chrono::steady_clock;
    UsageData update(pid_t pid, std::optional<ProcessSample> sample, Clock::time_point now) {
        UsageData result;
        if (!sample || sample->startTime == 0) {
            m_previous.erase(pid); // Failure must not masquerade as a zero-valued sample.
            return result;
        }
        result.available = true;
        result.cpuTime = sample->cpuTimeNs;
        result.memoryUsage = sample->physicalBytes;
        const auto found = m_previous.find(pid);
        if (found != m_previous.end() && found->second.sample.startTime == sample->startTime &&
            sample->cpuTimeNs >= found->second.sample.cpuTimeNs && now > found->second.time) {
            const auto elapsed = std::chrono::duration<double, std::nano>(now - found->second.time).count();
            result.cpuPercent = double(sample->cpuTimeNs - found->second.sample.cpuTimeNs) / elapsed;
            result.cpuAvailable = true;
        }
        m_previous[pid] = { *sample, now };
        return result;
    }

    UsageData sample(pid_t pid) { return update(pid, readProcessSample(pid), Clock::now()); }
    void retain(const std::unordered_set<pid_t>& live) {
        std::erase_if(m_previous, [&](const auto& item) { return !live.contains(item.first); });
    }
    void clear() { m_previous.clear(); }
private:
    struct Previous { ProcessSample sample; Clock::time_point time; };
    std::unordered_map<pid_t, Previous> m_previous;
};

} // namespace argus
