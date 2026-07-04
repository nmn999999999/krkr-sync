#pragma once

#include "krkr_sync/protocol.h"

namespace krkr_sync {

class TcpClient {
public:
    using MessageHandler = std::function<void(const Packet& pkt)>;
    using StatusHandler = std::function<void(bool connected)>;

    TcpClient();
    ~TcpClient();

    void connect(const std::string& host, uint16_t port);
    void disconnect();
    void send(MsgType type, const std::vector<uint8_t>& payload);
    bool is_connected() const;

    void set_message_handler(MessageHandler handler);
    void set_status_handler(StatusHandler handler);

private:
    void read_loop();

    SOCKET sock_ = INVALID_SOCKET;
    std::atomic<bool> connected_{false};
    std::atomic<uint32_t> sequence_{0};

    MessageHandler message_handler_;
    StatusHandler status_handler_;
    std::thread read_thread_;
};

}  // namespace krkr_sync
