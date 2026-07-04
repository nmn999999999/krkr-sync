#include "krkr_sync/tcp_server.h"
#include "krkr_sync/platform.h"

namespace krkr_sync {

TcpServer::TcpServer(uint16_t port) : port_(port) {
    platform::init_sockets();
}

TcpServer::~TcpServer() { stop(); }

void TcpServer::start() {
    if (running_) return;
    running_ = true;

    listen_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock_ == INVALID_SOCKET) return;

    int opt = 1;
    setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_sock_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listen_sock_);
        listen_sock_ = INVALID_SOCKET;
        return;
    }
    if (listen(listen_sock_, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listen_sock_);
        listen_sock_ = INVALID_SOCKET;
        return;
    }

    accept_thread_ = std::thread([this]() { accept_loop(); });
}

void TcpServer::stop() {
    if (!running_) return;
    running_ = false;

    if (listen_sock_ != INVALID_SOCKET) {
        closesocket(listen_sock_);
        listen_sock_ = INVALID_SOCKET;
    }

    {
        std::lock_guard lock(sessions_mutex_);
        for (auto& [id, session] : sessions_) {
            closesocket(session->sock);
            if (session->read_thread.joinable()) session->read_thread.detach();
        }
        sessions_.clear();
    }

    if (accept_thread_.joinable()) accept_thread_.join();
}

void TcpServer::send_to(uint32_t client_id, MsgType type, const std::vector<uint8_t>& payload) {
    std::lock_guard lock(sessions_mutex_);
    auto it = sessions_.find(client_id);
    if (it == sessions_.end()) return;

    Packet pkt;
    pkt.header.magic = PROTOCOL_MAGIC;
    pkt.header.version = PROTOCOL_VERSION;
    pkt.header.type = type;
    pkt.header.sequence = it->second->sequence.fetch_add(1);
    pkt.header.payload_size = static_cast<uint32_t>(payload.size());
    pkt.payload = payload;

    auto data = pkt.serialize();
    send(it->second->sock, (const char*)data.data(), (int)data.size(), 0);
}

void TcpServer::broadcast(MsgType type, const std::vector<uint8_t>& payload) {
    std::lock_guard lock(sessions_mutex_);
    for (auto& [id, session] : sessions_) {
        Packet pkt;
        pkt.header.magic = PROTOCOL_MAGIC;
        pkt.header.version = PROTOCOL_VERSION;
        pkt.header.type = type;
        pkt.header.sequence = session->sequence.fetch_add(1);
        pkt.header.payload_size = static_cast<uint32_t>(payload.size());
        pkt.payload = payload;

        auto data = pkt.serialize();
        send(session->sock, (const char*)data.data(), (int)data.size(), 0);
    }
}

void TcpServer::set_message_handler(MessageHandler handler) { message_handler_ = std::move(handler); }
void TcpServer::set_client_handler(ClientHandler handler) { client_handler_ = std::move(handler); }
size_t TcpServer::client_count() const { std::lock_guard lock(sessions_mutex_); return sessions_.size(); }

void TcpServer::accept_loop() {
    while (running_) {
        sockaddr_in client_addr{};
        int addr_len = sizeof(client_addr);
        SOCKET client_sock = accept(listen_sock_, (sockaddr*)&client_addr, &addr_len);
        if (client_sock == INVALID_SOCKET) continue;

        uint32_t client_id = next_client_id_++;
        auto session = std::make_shared<ClientSession>();
        session->sock = client_sock;

        {
            std::lock_guard lock(sessions_mutex_);
            sessions_[client_id] = session;
        }

        if (client_handler_) client_handler_(client_id, true);

        session->read_thread = std::thread([this, client_id, client_sock]() {
            client_read_loop(client_id, client_sock);
        });
    }
}

void TcpServer::client_read_loop(uint32_t client_id, SOCKET sock) {
    while (running_) {
        PacketHeader header{};
        int recvd = recv(sock, (char*)&header, sizeof(PacketHeader), MSG_WAITALL);
        if (recvd <= 0) break;

        header.from_network();
        if (header.magic != PROTOCOL_MAGIC || header.version != PROTOCOL_VERSION) break;

        Packet pkt;
        pkt.header = header;

        if (header.payload_size > 0) {
            pkt.payload.resize(header.payload_size);
            int total = 0;
            while (total < (int)header.payload_size) {
                int n = recv(sock, (char*)pkt.payload.data() + total,
                            (int)header.payload_size - total, 0);
                if (n <= 0) break;
                total += n;
            }
            if (total != (int)header.payload_size) break;
        }

        if (message_handler_) message_handler_(client_id, pkt);
    }
    remove_client(client_id);
}

void TcpServer::remove_client(uint32_t client_id) {
    SOCKET sock = INVALID_SOCKET;
    {
        std::lock_guard lock(sessions_mutex_);
        auto it = sessions_.find(client_id);
        if (it == sessions_.end()) return;
        sock = it->second->sock;
        sessions_.erase(it);
    }
    closesocket(sock);
    if (client_handler_) client_handler_(client_id, false);
}

}  // namespace krkr_sync
