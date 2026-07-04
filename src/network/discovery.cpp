#include "krkr_sync/discovery.h"
#include "krkr_sync/platform.h"

namespace krkr_sync {

DiscoveryService::DiscoveryService(const std::string& device_name, uint16_t listen_port)
    : device_name_(device_name), listen_port_(listen_port) {
    platform::init_sockets();
}

DiscoveryService::~DiscoveryService() { stop(); }

void DiscoveryService::start() {
    if (running_) return;
    running_ = true;

    recv_sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (recv_sock_ == INVALID_SOCKET) return;

    int opt = 1;
    setsockopt(recv_sock_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(DISCOVERY_PORT);
    bind(recv_sock_, (sockaddr*)&addr, sizeof(addr));

    // Join multicast group
    struct ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(DISCOVERY_GROUP);
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(recv_sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));

    send_sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (send_sock_ == INVALID_SOCKET) return;

    unsigned char ttl = 4;
    setsockopt(send_sock_, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, sizeof(ttl));

    recv_thread_ = std::thread([this]() { recv_loop(); });
}

void DiscoveryService::stop() {
    if (!running_) return;
    running_ = false;

    if (recv_sock_ != INVALID_SOCKET) { closesocket(recv_sock_); recv_sock_ = INVALID_SOCKET; }
    if (send_sock_ != INVALID_SOCKET) { closesocket(send_sock_); send_sock_ = INVALID_SOCKET; }
    if (recv_thread_.joinable()) recv_thread_.join();
}

void DiscoveryService::broadcast_presence() {
    if (send_sock_ == INVALID_SOCKET) return;

    std::string msg = std::string(DISCOVERY_MAGIC) + "|"
                    + device_name_ + "|"
                    + std::to_string(listen_port_) + "|"
                    + std::to_string(std::time(nullptr));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DISCOVERY_PORT);
    addr.sin_addr.s_addr = inet_addr(DISCOVERY_GROUP);

    sendto(send_sock_, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&addr, sizeof(addr));
}

std::vector<PeerInfo> DiscoveryService::get_peers() const {
    std::lock_guard lock(peers_mutex_);
    auto now = std::time(nullptr);
    std::vector<PeerInfo> alive;
    for (auto& p : peers_)
        if (now - p.timestamp < 30) alive.push_back(p);
    return alive;
}

void DiscoveryService::set_peer_callback(PeerCallback cb) { peer_callback_ = std::move(cb); }

void DiscoveryService::recv_loop() {
    char buf[1024];
    while (running_) {
        sockaddr_in sender{};
        SOCKLEN_T sender_len = sizeof(sender);
        int n = recvfrom(recv_sock_, buf, sizeof(buf) - 1, 0, (sockaddr*)&sender, &sender_len);
        if (n <= 0) continue;
        buf[n] = '\0';
        handle_message(buf, inet_ntoa(sender.sin_addr));
    }
}

void DiscoveryService::handle_message(const std::string& msg, const std::string& sender_ip) {
    if (msg.find(DISCOVERY_MAGIC) != 0) return;

    auto pos1 = msg.find('|');
    auto pos2 = msg.find('|', pos1 + 1);
    auto pos3 = msg.find('|', pos2 + 1);
    if (pos1 == std::string::npos || pos2 == std::string::npos) return;

    PeerInfo peer;
    peer.device_name = msg.substr(pos1 + 1, pos2 - pos1 - 1);
    peer.port = static_cast<uint16_t>(std::stoi(msg.substr(pos2 + 1, pos3 - pos2 - 1)));
    peer.ip_address = sender_ip;
    peer.timestamp = std::time(nullptr);

    if (sender_ip == "127.0.0.1" || peer.device_name == device_name_) return;

    {
        std::lock_guard lock(peers_mutex_);
        bool found = false;
        for (auto& existing : peers_) {
            if (existing.ip_address == peer.ip_address) {
                existing.timestamp = peer.timestamp;
                found = true;
                break;
            }
        }
        if (!found) peers_.push_back(peer);
    }

    if (peer_callback_) peer_callback_(peer);
}

}  // namespace krkr_sync
