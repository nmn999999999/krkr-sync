#import <Foundation/Foundation.h>
#include "krkr_sync/platform.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <CommonCrypto/CommonDigest.h>

namespace krkr_sync {
namespace platform {

static bool sockets_initialized = false;

void init_sockets() {
    sockets_initialized = true;
}

void cleanup_sockets() {
    sockets_initialized = false;
}

std::string get_device_name() {
    return std::string([[NSProcessInfo processInfo] hostName].UTF8String);
}

std::string get_data_dir() {
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    if ([paths count] > 0) {
        std::string base = [paths[0] UTF8String];
        std::string dir = base + "/krkr_sync";
        create_directory_recursive(dir);
        return dir;
    }
    return "/tmp/krkr_sync";
}

std::string get_temp_dir() {
    return "/tmp/krkr_sync";
}

std::string get_filename(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) return path.substr(pos + 1);
    return path;
}

bool create_directory_recursive(const std::string& path) {
    NSString *nsPath = [NSString stringWithUTF8String:path.c_str()];
    NSFileManager *fm = [NSFileManager defaultManager];
    NSError *error = nil;
    [fm createDirectoryAtPath:nsPath withIntermediateDirectories:YES attributes:nil error:&error];
    return error == nil;
}

bool file_exists(const std::string& path) {
    NSString *nsPath = [NSString stringWithUTF8String:path.c_str()];
    return [[NSFileManager defaultManager] fileExistsAtPath:nsPath];
}

bool is_directory(const std::string& path) {
    NSString *nsPath = [NSString stringWithUTF8String:path.c_str()];
    BOOL isDir = NO;
    BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:nsPath isDirectory:&isDir];
    return exists && isDir;
}

std::vector<std::string> list_directory(const std::string& path) {
    std::vector<std::string> result;
    NSString *nsPath = [NSString stringWithUTF8String:path.c_str()];
    NSArray *contents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:nsPath error:nil];
    for (NSString *item in contents) {
        std::string name = [item UTF8String];
        if (name != "." && name != "..") result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

uint64_t get_file_size(const std::string& path) {
    NSString *nsPath = [NSString stringWithUTF8String:path.c_str()];
    NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:nsPath error:nil];
    return [attrs[NSFileSize] unsignedLongLongValue];
}

uint64_t get_file_modified_time(const std::string& path) {
    NSString *nsPath = [NSString stringWithUTF8String:path.c_str()];
    NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:nsPath error:nil];
    NSDate *modDate = attrs[NSFileModificationDate];
    return (uint64_t)[modDate timeIntervalSince1970];
}

std::vector<uint8_t> read_file(const std::string& path) {
    NSString *nsPath = [NSString stringWithUTF8String:path.c_str()];
    NSData *data = [NSData dataWithContentsOfFile:nsPath];
    if (!data) return {};
    return std::vector<uint8_t>((const uint8_t*)data.bytes, (const uint8_t*)data.bytes + data.length);
}

bool write_file(const std::string& path, const std::vector<uint8_t>& data) {
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        std::string dir = path.substr(0, pos);
        create_directory_recursive(dir);
    }
    NSString *nsPath = [NSString stringWithUTF8String:path.c_str()];
    NSData *nsData = [NSData dataWithBytes:data.data() length:data.size()];
    return [nsData writeToFile:nsPath atomically:YES];
}

std::vector<std::string> get_local_ips() {
    std::vector<std::string> ips;
    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces) == 0) {
        for (struct ifaddrs *ifa = interfaces; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;
            if (!(ifa->ifa_flags & IFF_UP)) continue;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr, ip, sizeof(ip));
            std::string ipStr(ip);
            if (ipStr != "127.0.0.1") ips.push_back(ipStr);
        }
        freeifaddrs(interfaces);
    }
    return ips;
}

std::vector<uint8_t> compute_md5(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> digest(CC_MD5_DIGEST_LENGTH);
    CC_MD5(data.data(), (CC_LONG)data.size(), digest.data());
    return digest;
}

std::vector<uint8_t> compute_file_md5(const std::string& filepath) {
    return compute_md5(read_file(filepath));
}

}  // namespace platform
}  // namespace krkr_sync

extern "C" {

std::string krkr_sync_ios_http_post(const std::string& url_str,
                                     const std::string& api_key,
                                     const std::string& json_body) {
    @autoreleasepool {
        NSURL *url = [NSURL URLWithString:[NSString stringWithUTF8String:url_str.c_str()]];
        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
        [request setHTTPMethod:@"POST"];
        [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];

        NSString *authStr = [NSString stringWithFormat:@"Bearer %s", api_key.c_str()];
        [request setValue:authStr forHTTPHeaderField:@"Authorization"];

        NSString *bodyStr = [NSString stringWithUTF8String:json_body.c_str()];
        [request setHTTPBody:[bodyStr dataUsingEncoding:NSUTF8StringEncoding]];

        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        __block std::string response;

        NSURLSessionDataTask *task = [[NSURLSession sharedSession] dataTaskWithRequest:request
            completionHandler:^(NSData *data, NSURLResponse *urlResponse, NSError *error) {
                if (data && !error) {
                    NSString *str = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
                    response = [str UTF8String];
                }
                dispatch_semaphore_signal(semaphore);
            }];
        [task resume];
        dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);

        return response.empty() ? R"({"success":false,"error":"Empty response"})" : response;
    }
}

}  // extern "C"
