//
//  ProcessUtils.cpp
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/11/27.
//

#include "ProcessUtils.hpp"
#include <sys/sysctl.h>
#include "WebKitUtils.h"

bool processUtilsGetProcessArguments(pid_t pid, std::vector<std::string>* argv) {
    size_t args_size_estimate;
    size_t args_size;
    std::string args;
    int tries = 3;
    do {
        int mib[] = {CTL_KERN, KERN_PROCARGS2, pid};
        int rv = sysctl(mib, std::size(mib), nullptr, &args_size_estimate, nullptr, 0);
        if (rv != 0) {
            return false;
        }
        args_size = args_size_estimate + 32;
        args.resize(args_size);
        rv = sysctl(mib, std::size(mib), &args[0], &args_size, nullptr, 0);
        if (rv != 0) {
            return false;
        }
    } while (args_size == args_size_estimate + 1 && tries--);
    if (args_size == args_size_estimate + 1) {
        return false;
    }
    // KERN_PROCARGS2 needs to at least contain argc.
    if (args_size < sizeof(int)) {
        return false;
    }
    args.resize(args_size);
    // Get argc.
    int argc;
    memcpy(&argc, &args[0], sizeof(argc));
    // Find the end of the executable path.
    size_t start_pos = sizeof(argc);
    size_t nul_pos = args.find('\0', start_pos);
    if (nul_pos == std::string::npos) {
        return false;
    }
    // Find the beginning of the string area.
    start_pos = args.find_first_not_of('\0', nul_pos);
    if (start_pos == std::string::npos) {
        return false;
    }
    std::vector<std::string> local_argv;
    while (argc-- && nul_pos != std::string::npos) {
        nul_pos = args.find('\0', start_pos);
        local_argv.push_back(args.substr(start_pos, nul_pos - start_pos));
        start_pos = nul_pos + 1;
    }
    if (argc >= 0) {
        // Not every argument was recovered.
        return false;
    }
    argv->swap(local_argv);
    return true;
}

std::string processUtilsGetWebKitActiveUrl(pid_t pid) {
    @autoreleasepool {
        NSString *url = getWebKitActiveURL(pid);
        if (!url) {
            url = getWebKitDisplayName(pid);
        }
        if (!url) {
            return {};
        }
        return std::string([url UTF8String]);
    }
}
