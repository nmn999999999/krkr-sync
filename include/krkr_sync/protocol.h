#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
typedef int SOCKLEN_T;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
typedef int SOCKET;
typedef socklen_t SOCKLEN_T;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define closesocket(s) close(s)
#endif

#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <cstring>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>

namespace krkr_sync {

constexpr uint16_t PROTOCOL_MAGIC = 0x4B52;
constexpr uint8_t PROTOCOL_VERSION = 1;

enum class MsgType : uint8_t {
    HEARTBEAT       = 0x01,
    HEARTBEAT_ACK   = 0x02,
    GAME_LIST_REQ   = 0x10,
    GAME_LIST_RESP  = 0x11,
    SAVE_SYNC_REQ   = 0x20,
    SAVE_SYNC_RESP  = 0x21,
    SAVE_DATA       = 0x22,
    SAVE_ACK        = 0x23,
    IDENTIFY_REQ    = 0x40,
    IDENTIFY_RESP   = 0x41,
    ERROR_MSG       = 0xFF,
};

#pragma pack(push, 1)
struct PacketHeader {
    uint16_t magic;
    uint8_t  version;
    MsgType  type;
    uint32_t sequence;
    uint32_t payload_size;

    void to_network() {
        magic = htons(magic);
        sequence = htonl(sequence);
        payload_size = htonl(payload_size);
    }
    void from_network() { to_network(); }
};
#pragma pack(pop)

struct Packet {
    PacketHeader header{};
    std::vector<uint8_t> payload;

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> result(sizeof(PacketHeader) + payload.size());
        PacketHeader hdr = header;
        hdr.to_network();
        std::memcpy(result.data(), &hdr, sizeof(PacketHeader));
        if (!payload.empty())
            std::memcpy(result.data() + sizeof(PacketHeader), payload.data(), payload.size());
        return result;
    }

    static bool deserialize(const uint8_t* data, size_t len, Packet& out) {
        if (len < sizeof(PacketHeader)) return false;
        std::memcpy(&out.header, data, sizeof(PacketHeader));
        out.header.from_network();
        if (out.header.magic != PROTOCOL_MAGIC) return false;
        if (out.header.version != PROTOCOL_VERSION) return false;
        if (out.header.payload_size > len - sizeof(PacketHeader)) return false;
        out.payload.resize(out.header.payload_size);
        if (!out.payload.empty())
            std::memcpy(out.payload.data(), data + sizeof(PacketHeader), out.header.payload_size);
        return true;
    }
};

struct GameInfo {
    std::string id, name, engine, folder_path;
    uint64_t last_modified = 0;
    uint64_t total_size = 0;

    std::vector<uint8_t> serialize() const;
    static GameInfo deserialize(const std::vector<uint8_t>& data);
};

struct SaveEntry {
    std::string game_id, relative_path;
    uint64_t file_size = 0;
    uint64_t modified_time = 0;
    std::array<uint8_t, 16> md5{};

    std::vector<uint8_t> serialize() const;
    static SaveEntry deserialize(const std::vector<uint8_t>& data);
};

std::vector<uint8_t> serialize_string(const std::string& s);
std::string deserialize_string(const uint8_t* data, size_t len, size_t& offset);
std::vector<uint8_t> serialize_u64(uint64_t v);
uint64_t deserialize_u64(const uint8_t* data, size_t& offset);

}  // namespace krkr_sync
