#include "../native-common/ProcessUsage.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>

int main() {
    using namespace std::chrono;
    using Clock = argus::ProcessUsageTracker::Clock;
    assert(argus::ticksToNanoseconds(24, 125, 3) == 1000);
    assert(argus::ticksToNanoseconds(UINT64_MAX, 2, 2) == UINT64_MAX);
    assert(!argus::ticksToNanoseconds(UINT64_MAX, 2, 1));
    assert(!argus::ticksToNanoseconds(1, 1, 0));
    argus::ProcessUsageTracker tracker;
    const Clock::time_point begin(seconds(1));
    assert(!tracker.update(1, argus::ProcessSample{10, 100000000, 123}, begin).cpuAvailable);
    // Actual interval is 4s, despite any configured 2s timer; 2 CPU seconds = 50%.
    auto value = tracker.update(1, argus::ProcessSample{10, 2100000000, 456}, begin + seconds(4));
    assert(value.cpuAvailable && std::abs(value.cpuPercent - 0.5) < 1e-12 && value.memoryUsage == 456);
    value = tracker.update(1, argus::ProcessSample{10, 6100000000, 456}, begin + seconds(6));
    assert(value.cpuPercent == 2); // Multicore usage is not clamped to 100%.
    assert(!tracker.update(1, argus::ProcessSample{11, 1, 456}, begin + seconds(7)).cpuAvailable); // PID reuse.
    assert(!tracker.update(1, argus::ProcessSample{11, 0, 456}, begin + seconds(8)).cpuAvailable); // Counter reset.
    assert(!tracker.update(1, argus::ProcessSample{11, 1, 456}, begin + seconds(8)).cpuAvailable); // Zero interval.
    assert(!tracker.update(1, std::nullopt, begin + seconds(9)).available);
    assert(!tracker.update(1, argus::ProcessSample{11, 100, 456}, begin + seconds(10)).cpuAvailable);
    tracker.retain({});
    assert(!tracker.update(1, argus::ProcessSample{11, 200, 456}, begin + seconds(11)).cpuAvailable);
    assert(!argus::readProcessSample(-1));

    // Independent getrusage calibration catches accidental Mach tick conversion.
    auto cpuNs = [] {
        rusage raw {};
        assert(getrusage(RUSAGE_SELF, &raw) == 0);
        return uint64_t(raw.ru_utime.tv_sec + raw.ru_stime.tv_sec) * 1000000000 +
            uint64_t(raw.ru_utime.tv_usec + raw.ru_stime.tv_usec) * 1000;
    };
    const auto before = *argus::readProcessSample(getpid());
    const auto first = cpuNs();
    uint64_t counter = 0;
    while (cpuNs() - first < 100000000) { ++counter; }
    const auto elapsed = cpuNs() - first;
    const auto after = *argus::readProcessSample(getpid());
    const double ratio = double(after.cpuTimeNs - before.cpuTimeNs) / double(elapsed);
    assert(counter > 0 && ratio > 0.8 && ratio < 1.2);
    std::cout << "PASS: CPU units, real intervals, PID reuse, failures, counter resets; getrusage ratio=" << ratio << '\n';
}
