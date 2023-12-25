//
//  ProcessUtils.hpp
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/11/27.
//

#ifndef ProcessUtils_hpp
#define ProcessUtils_hpp

#include <libproc.h>
#include <vector>
#include <string>

bool processUtilsGetProcessArguments(pid_t pid, std::vector<std::string>* argv);
std::string processUtilsGetWebKitActiveUrl(pid_t pid);

#endif /* ProcessUtils_hpp */
