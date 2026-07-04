#pragma once

#include "krkr_sync/protocol.h"

namespace krkr_sync {

constexpr uint16_t DISCOVERY_PORT = 29527;
constexpr const char* DISCOVERY_MAGIC = "KRKR_SYNC";
constexpr const char* DISCOVERY_GROUP = "239.255.255.250";

struct PeerInfo {
    std::string device_name;
    std::string ip_address;
    uint16_t    port = 0;
    uint64_t    timestamp = 0;
};

class DiscoveryService {
public:
    using PeerCallback = std::function<void(const PeerInfo& peer)>;

    DiscoveryService(const std::string& device_name, uint16_t listen_port);
    ~DiscoveryService();

    void start();
    void stop();
    void broadcast_presence();
    std::vector<PeerInfo> get_peers() const;
    void set_peer_callback(PeerCallback cb);

private:
    void recv_loop();
    void handle_message(const std::string& msg, const std::string& sender_ip);

    SOCKET send_sock_ = INVALID_SOCKET;
    SOCKET recv_sock_ = INVALID_SOCKET;
    std::string device_name_;
    uint16_t listen_port_;

    std::vector<PeerInfo> peers_;
    mutable std::mutex peers_mutex_;
    PeerCallback peer_callback_;
    std::atomic<bool> running_{false};
    std::thread recv_thread_;
};

}  // namespace krkr_sync
