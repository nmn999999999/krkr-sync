#include "krkr_sync/sync_engine.h"
#include "krkr_sync/platform.h"

namespace krkr_sync {

SyncEngine::SyncEngine(const SyncConfig& config) : config_(config) {
    scanner_.set_games_folder(config.games_folder);
}

SyncEngine::~SyncEngine() { stop(); }

bool SyncEngine::start() {
    std::string data_dir = platform::get_data_dir();
    platform::create_directory_recursive(data_dir);

    server_ = std::make_unique<TcpServer>(config_.sync_port);
    server_->set_message_handler([this](uint32_t cid, const Packet& pkt) { on_server_message(cid, pkt); });
    server_->set_client_handler([this](uint32_t cid, bool conn) {
        if (status_callback_) status_callback_(conn ? "Client connected: " + std::to_string(cid) : "Client disconnected: " + std::to_string(cid));
    });

    if (config_.auto_discover) {
        discovery_ = std::make_unique<DiscoveryService>(config_.device_name, config_.sync_port);
        discovery_->set_peer_callback([this](const PeerInfo& p) { on_peer_discovered(p); });
    }

    scanner_.set_progress_callback([this](const std::string& game, int total, int idx) {
        if (progress_callback_) progress_callback_("Scanning: " + game, total > 0 ? (idx * 100) / total : 0);
    });

    auto games = scanner_.scan_all();
    if (status_callback_) status_callback_("Found " + std::to_string(games.size()) + " games");

    server_->start();
    if (discovery_) { discovery_->start(); discovery_->broadcast_presence(); }
    return true;
}

void SyncEngine::stop() {
    if (discovery_) discovery_->stop();
    if (server_) server_->stop();
    if (client_) client_->disconnect();
}

std::vector<DetectedGame> SyncEngine::get_local_games() const {
    std::vector<DetectedGame> games;
    for (auto& [_, game] : scanner_.get_cached_games()) games.push_back(game);
    return games;
}

std::vector<PeerInfo> SyncEngine::get_discovered_peers() const {
    return discovery_ ? discovery_->get_peers() : std::vector<PeerInfo>{};
}

bool SyncEngine::request_game_list(uint32_t peer_client_id) {
    server_->send_to(peer_client_id, MsgType::GAME_LIST_REQ, {});
    return true;
}

bool SyncEngine::request_save_sync(uint32_t peer_client_id, const std::string& game_id) {
    server_->send_to(peer_client_id, MsgType::SAVE_SYNC_REQ, serialize_string(game_id));
    return true;
}

void SyncEngine::set_status_callback(StatusCallback cb) { status_callback_ = std::move(cb); }
void SyncEngine::set_progress_callback(ProgressCallback cb) { progress_callback_ = std::move(cb); }

void SyncEngine::on_server_message(uint32_t client_id, const Packet& pkt) {
    switch (pkt.header.type) {
        case MsgType::HEARTBEAT: server_->send_to(client_id, MsgType::HEARTBEAT_ACK, {}); break;
        case MsgType::GAME_LIST_REQ: handle_game_list_request(client_id); break;
        case MsgType::SAVE_SYNC_REQ: handle_save_sync_request(client_id, pkt); break;
        case MsgType::SAVE_DATA: handle_save_data(client_id, pkt); break;
        default: break;
    }
}

void SyncEngine::on_client_message(const Packet& pkt) {
    switch (pkt.header.type) {
        case MsgType::GAME_LIST_RESP: handle_game_list_response(pkt); break;
        default: break;
    }
}

void SyncEngine::on_client_status(bool connected) {
    if (status_callback_) status_callback_(connected ? "Connected to server" : "Disconnected");
}

void SyncEngine::on_peer_discovered(const PeerInfo& peer) {
    if (status_callback_) status_callback_("Discovered: " + peer.device_name + " at " + peer.ip_address);
    if (!client_ || !client_->is_connected()) {
        client_ = std::make_unique<TcpClient>();
        client_->set_message_handler([this](const Packet& pkt) { on_client_message(pkt); });
        client_->set_status_handler([this](bool c) { on_client_status(c); });
        client_->connect(peer.ip_address, peer.port);
    }
}

void SyncEngine::handle_game_list_request(uint32_t client_id) {
    auto& games = scanner_.get_cached_games();
    std::vector<uint8_t> payload;
    uint32_t count = (uint32_t)games.size();
    payload.insert(payload.end(), (uint8_t*)&count, (uint8_t*)&count + sizeof(count));

    for (auto& [_, game] : games) {
        GameInfo info;
        info.id = game.folder_name;
        info.name = game.detected_name.empty() ? game.folder_name : game.detected_name;
        info.engine = game.engine_str;
        info.folder_path = game.folder_path;
        auto gd = info.serialize();
        uint32_t gs = (uint32_t)gd.size();
        payload.insert(payload.end(), (uint8_t*)&gs, (uint8_t*)&gs + sizeof(gs));
        payload.insert(payload.end(), gd.begin(), gd.end());
    }
    server_->send_to(client_id, MsgType::GAME_LIST_RESP, payload);
    if (status_callback_) status_callback_("Sent game list (" + std::to_string(count) + " games)");
}

void SyncEngine::handle_game_list_response(const Packet& pkt) {
    if (pkt.payload.size() < sizeof(uint32_t)) return;
    uint32_t count = 0;
    std::memcpy(&count, pkt.payload.data(), sizeof(uint32_t));
    size_t offset = sizeof(uint32_t);

    std::lock_guard lock(remote_games_mutex_);
    remote_games_.clear();
    for (uint32_t i = 0; i < count; ++i) {
        if (offset + sizeof(uint32_t) > pkt.payload.size()) break;
        uint32_t gs = 0;
        std::memcpy(&gs, pkt.payload.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        if (offset + gs > pkt.payload.size()) break;
        std::vector<uint8_t> gd(pkt.payload.begin() + offset, pkt.payload.begin() + offset + gs);
        offset += gs;
        auto info = GameInfo::deserialize(gd);
        DetectedGame g; g.folder_name = info.id; g.detected_name = info.name; g.engine_str = info.engine;
        remote_games_[info.id] = g;
    }
    if (status_callback_) status_callback_("Received " + std::to_string(count) + " remote games");
}

void SyncEngine::handle_save_sync_request(uint32_t client_id, const Packet& pkt) {
    size_t offset = 0;
    std::string game_id = deserialize_string(pkt.payload.data(), pkt.payload.size(), offset);
    std::string game_save_dir = platform::get_data_dir() + "\\saves\\" + game_id;

    std::vector<uint8_t> payload;
    auto files = platform::list_directory(game_save_dir);
    uint32_t fc = (uint32_t)files.size();
    payload.insert(payload.end(), (uint8_t*)&fc, (uint8_t*)&fc + sizeof(fc));

    for (auto& file : files) {
        std::string fp = game_save_dir + "\\" + file;
        SaveEntry entry;
        entry.game_id = game_id;
        entry.relative_path = file;
        entry.file_size = platform::get_file_size(fp);
        entry.modified_time = platform::get_file_modified_time(fp);
        auto md5 = platform::compute_file_md5(fp);
        std::memcpy(entry.md5.data(), md5.data(), 16);
        auto ed = entry.serialize();
        uint32_t es = (uint32_t)ed.size();
        payload.insert(payload.end(), (uint8_t*)&es, (uint8_t*)&es + sizeof(es));
        payload.insert(payload.end(), ed.begin(), ed.end());
    }
    server_->send_to(client_id, MsgType::SAVE_SYNC_RESP, payload);
}

void SyncEngine::handle_save_data(uint32_t client_id, const Packet& pkt) {
    SaveEntry entry = SaveEntry::deserialize(pkt.payload);
    size_t data_offset = sizeof(uint32_t) * 3 + entry.game_id.size() + entry.relative_path.size() + 16 + 8 + 8 + 4;
    if (data_offset > pkt.payload.size()) return;

    std::vector<uint8_t> file_data(pkt.payload.begin() + data_offset, pkt.payload.end());
    std::string save_dir = platform::get_data_dir() + "\\saves\\" + entry.game_id;
    platform::create_directory_recursive(save_dir);

    std::string file_path = save_dir + "\\" + entry.relative_path;
    if (platform::write_file(file_path, file_data)) {
        server_->send_to(client_id, MsgType::SAVE_ACK, {});
        if (status_callback_) status_callback_("Received save: " + entry.relative_path);
    }
}

}  // namespace krkr_sync
