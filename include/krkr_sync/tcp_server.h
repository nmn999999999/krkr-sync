#pragma once

#include "krkr_sync/protocol.h"

namespace krkr_sync {

class TcpServer {
public:
    using MessageHandler = std::function<void(uint32_t client_id, const Packet& pkt)>;
    using ClientHandler = std::function<void(uint32_t client_id, bool connected)>;

    TcpServer(uint16_t port);
    ~TcpServer();

    void start();
    void stop();
    void send_to(uint32_t client_id, MsgType type, const std::vector<uint8_t>& payload);
    void broadcast(MsgType type, const std::vector<uint8_t>& payload);

    void set_message_handler(MessageHandler handler);
    void set_client_handler(ClientHandler handler);
    size_t client_count() const;

private:
    void accept_loop();
    void client_read_loop(uint32_t client_id, SOCKET sock);
    void remove_client(uint32_t client_id);

    SOCKET listen_sock_ = INVALID_SOCKET;
    uint16_t port_;

    struct ClientSession {
        SOCKET sock;
        std::atomic<uint32_t> sequence{0};
        std::thread read_thread;
    };

    std::unordered_map<uint32_t, std::shared_ptr<ClientSession>> sessions_;
    mutable std::mutex sessions_mutex_;
    uint32_t next_client_id_ = 1;

    MessageHandler message_handler_;
    ClientHandler client_handler_;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
};

}  // namespace krkr_sync
