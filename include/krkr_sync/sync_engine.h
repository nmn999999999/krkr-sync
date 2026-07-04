#pragma once

#include "krkr_sync/protocol.h"
#include "krkr_sync/game_scanner.h"
#include "krkr_sync/tcp_server.h"
#include "krkr_sync/tcp_client.h"
#include "krkr_sync/discovery.h"

namespace krkr_sync {

struct SyncConfig {
    std::string games_folder;
    std::string device_name;
    uint16_t    sync_port = 29526;
    bool        auto_discover = true;
};

class SyncEngine {
public:
    using StatusCallback = std::function<void(const std::string& msg)>;
    using ProgressCallback = std::function<void(const std::string& game, int progress)>;

    SyncEngine(const SyncConfig& config);
    ~SyncEngine();

    bool start();
    void stop();
    std::vector<DetectedGame> get_local_games() const;
    std::vector<PeerInfo> get_discovered_peers() const;
    bool request_game_list(uint32_t peer_client_id);
    bool request_save_sync(uint32_t peer_client_id, const std::string& game_id);

    void set_status_callback(StatusCallback cb);
    void set_progress_callback(ProgressCallback cb);

private:
    void on_server_message(uint32_t client_id, const Packet& pkt);
    void on_client_message(const Packet& pkt);
    void on_client_status(bool connected);
    void on_peer_discovered(const PeerInfo& peer);
    void handle_game_list_request(uint32_t client_id);
    void handle_game_list_response(const Packet& pkt);
    void handle_save_sync_request(uint32_t client_id, const Packet& pkt);
    void handle_save_data(uint32_t client_id, const Packet& pkt);

    SyncConfig config_;
    GameScanner scanner_;
    std::unique_ptr<TcpServer> server_;
    std::unique_ptr<TcpClient> client_;
    std::unique_ptr<DiscoveryService> discovery_;
    StatusCallback status_callback_;
    ProgressCallback progress_callback_;
    std::unordered_map<std::string, DetectedGame> remote_games_;
    mutable std::mutex remote_games_mutex_;
};

}  // namespace krkr_sync
