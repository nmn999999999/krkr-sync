#pragma once

#include "krkr_sync/protocol.h"
#include <functional>
#include <unordered_map>

namespace krkr_sync {

enum class EngineType { UNKNOWN, KRKR, RENPY, NSCRIPTER, YU_RIS, REALLIVE };

struct DetectedGame {
    std::string folder_name;
    std::string folder_path;
    std::string detected_name;
    EngineType engine = EngineType::UNKNOWN;
    std::string engine_str;
    std::string exe_path;
    std::vector<std::string> save_paths;
    std::vector<std::string> data_files;
};

class GameScanner {
public:
    using ProgressCallback = std::function<void(const std::string& current, int total, int current_idx)>;

    void set_games_folder(const std::string& path);
    void set_progress_callback(ProgressCallback cb);
    std::vector<DetectedGame> scan_all();
    DetectedGame scan_single(const std::string& folder_path);
    EngineType detect_engine(const std::string& folder_path);
    const std::unordered_map<std::string, DetectedGame>& get_cached_games() const;

private:
    bool check_krkr(const std::string& path, DetectedGame& game);
    bool check_renpy(const std::string& path, DetectedGame& game);
    bool check_nscripter(const std::string& path, DetectedGame& game);
    bool check_yuris(const std::string& path, DetectedGame& game);
    bool check_reallive(const std::string& path, DetectedGame& game);
    void find_save_files(const std::string& path, DetectedGame& game);
    void find_data_files(const std::string& path, DetectedGame& game);
    std::string to_lower(const std::string& s);
    bool ends_with(const std::string& s, const std::string& suffix);

    std::string games_folder_;
    ProgressCallback progress_callback_;
    std::unordered_map<std::string, DetectedGame> game_cache_;
};

}  // namespace krkr_sync
