//
//  WebKitUtils.h
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/11/28.
//

#ifndef WebKitUtils_h
#define WebKitUtils_h

#import <Foundation/Foundation.h>

FOUNDATION_EXPORT NSString *getWebKitActiveURL(pid_t pid);
FOUNDATION_EXPORT NSString *getWebKitDisplayName(pid_t pid);

#endif /* WebKitUtils_h */
