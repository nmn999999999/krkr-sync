#include "krkr_sync/platform.h"
#include <shlobj.h>
#include <direct.h>
#include <io.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")

#ifndef MAX_COMPUTERNAME
#define MAX_COMPUTERNAME 256
#endif

namespace krkr_sync {
namespace platform {

static bool wsa_initialized = false;

void init_sockets() {
    if (!wsa_initialized) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        wsa_initialized = true;
    }
}

void cleanup_sockets() {
    if (wsa_initialized) {
        WSACleanup();
        wsa_initialized = false;
    }
}

std::string get_device_name() {
    char name[MAX_COMPUTERNAME + 1];
    DWORD size = MAX_COMPUTERNAME + 1;
    GetComputerNameA(name, &size);
    return std::string(name);
}

std::string get_data_dir() {
    char path[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path);
    return std::string(path) + "\\krkr_sync";
}

std::string get_temp_dir() {
    char path[MAX_PATH];
    GetTempPathA(MAX_PATH, path);
    return std::string(path) + "krkr_sync";
}

std::string get_filename(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) return path.substr(pos + 1);
    return path;
}

bool create_directory_recursive(const std::string& path) {
    int ret = _mkdir(path.c_str());
    if (ret == 0 || errno == EEXIST) return true;
    size_t pos = path.find_last_of('\\');
    if (pos != std::string::npos) {
        std::string parent = path.substr(0, pos);
        if (create_directory_recursive(parent)) {
            ret = _mkdir(path.c_str());
            return ret == 0 || errno == EEXIST;
        }
    }
    return false;
}

bool file_exists(const std::string& path) {
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool is_directory(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

std::vector<std::string> list_directory(const std::string& path) {
    std::vector<std::string> result;
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((path + "\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return result;
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        result.push_back(name);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    std::sort(result.begin(), result.end());
    return result;
}

uint64_t get_file_size(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) {
        ULARGE_INTEGER size;
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        return size.QuadPart;
    }
    return 0;
}

uint64_t get_file_modified_time(const std::string& path) {
    HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 0;
    FILETIME ft;
    GetFileTime(hFile, NULL, NULL, &ft);
    CloseHandle(hFile);
    ULARGE_INTEGER time;
    time.HighPart = ft.dwHighDateTime;
    time.LowPart = ft.dwLowDateTime;
    return time.QuadPart / 10000000ULL - 11644473600ULL;
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    auto size = file.tellg();
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& data) {
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        std::string dir = path.substr(0, pos);
        create_directory_recursive(dir);
    }
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return file.good();
}

std::vector<std::string> get_local_ips() {
    init_sockets();
    std::vector<std::string> ips;
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints{}, *result;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(hostname, NULL, &hints, &result) == 0) {
            for (auto* p = result; p; p = p->ai_next) {
                auto* addr = reinterpret_cast<sockaddr_in*>(p->ai_addr);
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
                ips.push_back(ip);
            }
            freeaddrinfo(result);
        }
    }
    return ips;
}

std::vector<uint8_t> compute_md5(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> digest(16);
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash);
    CryptHashData(hHash, data.data(), static_cast<DWORD>(data.size()), 0);
    DWORD size = 16;
    CryptGetHashParam(hHash, HP_HASHVAL, digest.data(), &size, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return digest;
}

std::vector<uint8_t> compute_file_md5(const std::string& filepath) {
    return compute_md5(read_file(filepath));
}

}  // namespace platform
}  // namespace krkr_sync
