#include "krkr_sync/tcp_client.h"
#include "krkr_sync/platform.h"

namespace krkr_sync {

TcpClient::TcpClient() { platform::init_sockets(); }
TcpClient::~TcpClient() { disconnect(); }

void TcpClient::connect(const std::string& host, uint16_t port) {
    if (connected_) return;

    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // Try to resolve hostname, fallback to direct IP
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        struct addrinfo hints{}, *result;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) == 0) {
            if (result) {
                std::memcpy(&addr, result->ai_addr, sizeof(addr));
                freeaddrinfo(result);
            }
        }
    }

    if (::connect(sock_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        if (status_handler_) status_handler_(false);
        return;
    }

    connected_ = true;
    if (status_handler_) status_handler_(true);
    read_thread_ = std::thread([this]() { read_loop(); });
}

void TcpClient::disconnect() {
    if (!connected_) return;
    connected_ = false;

    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }

    if (read_thread_.joinable()) read_thread_.join();
}

void TcpClient::send(MsgType type, const std::vector<uint8_t>& payload) {
    if (!connected_) return;

    Packet pkt;
    pkt.header.magic = PROTOCOL_MAGIC;
    pkt.header.version = PROTOCOL_VERSION;
    pkt.header.type = type;
    pkt.header.sequence = sequence_.fetch_add(1);
    pkt.header.payload_size = static_cast<uint32_t>(payload.size());
    pkt.payload = payload;

    auto data = pkt.serialize();
    ::send(sock_, (const char*)data.data(), (int)data.size(), 0);
}

bool TcpClient::is_connected() const { return connected_; }
void TcpClient::set_message_handler(MessageHandler handler) { message_handler_ = std::move(handler); }
void TcpClient::set_status_handler(StatusHandler handler) { status_handler_ = std::move(handler); }

void TcpClient::read_loop() {
    while (connected_) {
        PacketHeader header{};
        int recvd = recv(sock_, (char*)&header, sizeof(PacketHeader), MSG_WAITALL);
        if (recvd <= 0) break;

        header.from_network();
        if (header.magic != PROTOCOL_MAGIC || header.version != PROTOCOL_VERSION) break;

        Packet pkt;
        pkt.header = header;

        if (header.payload_size > 0) {
            pkt.payload.resize(header.payload_size);
            int total = 0;
            while (total < (int)header.payload_size) {
                int n = recv(sock_, (char*)pkt.payload.data() + total,
                            (int)header.payload_size - total, 0);
                if (n <= 0) break;
                total += n;
            }
            if (total != (int)header.payload_size) break;
        }

        if (message_handler_) message_handler_(pkt);
    }
    connected_ = false;
    if (status_handler_) status_handler_(false);
}

}  // namespace krkr_sync
