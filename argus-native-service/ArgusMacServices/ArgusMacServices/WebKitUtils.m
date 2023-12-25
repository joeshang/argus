//
//  WebKitUtils.m
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/11/28.
//

#import <Foundation/Foundation.h>

const CFStringRef kLSActivePageUserVisibleOriginsKey = CFSTR("LSActivePageUserVisibleOriginsKey");
const CFStringRef kLSDisplayName = CFSTR("LSDisplayName");
const int kLSMagicConstant = -2;

extern CFTypeRef _LSCopyApplicationInformationItem(int /* hopefully */, CFTypeRef, CFStringRef);
extern CFTypeRef _LSASNCreateWithPid(CFAllocatorRef, pid_t);
extern pid_t responsibility_get_pid_responsible_for_pid(pid_t);

NSString *getWebKitActiveURL(pid_t pid) {
    NSString *url = nil;
    CFTypeRef asn = _LSASNCreateWithPid(kCFAllocatorDefault, pid);
    
    if (asn != NULL) {
        id information = CFBridgingRelease(_LSCopyApplicationInformationItem(kLSMagicConstant, asn, kLSActivePageUserVisibleOriginsKey));

        if ([information isKindOfClass:[NSString class]]) {
            url = information;
        } else if ([information isKindOfClass:[NSArray class]]) {
            NSArray *array = (NSArray *)information;
            if (array.count > 0) {
                url = array[0];
            }
        }
        
        CFRelease(asn);
    }
    
    return url;
}

NSString *getWebKitDisplayName(pid_t pid) {
    NSString *name = nil;
    CFTypeRef asn = _LSASNCreateWithPid(kCFAllocatorDefault, pid);

    if (asn != NULL) {
        id information = CFBridgingRelease(_LSCopyApplicationInformationItem(kLSMagicConstant, asn, kLSDisplayName));

        if ([information isKindOfClass:[NSString class]]) {
            name = information;
        }

        CFRelease(asn);
    }

    return name;
}
