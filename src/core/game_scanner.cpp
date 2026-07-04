#include "krkr_sync/game_scanner.h"
#include "krkr_sync/platform.h"

namespace krkr_sync {

void GameScanner::set_games_folder(const std::string& path) { games_folder_ = path; }
void GameScanner::set_progress_callback(ProgressCallback cb) { progress_callback_ = std::move(cb); }

std::string GameScanner::to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

bool GameScanner::ends_with(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return to_lower(s).compare(s.size() - suffix.size(), suffix.size(), to_lower(suffix)) == 0;
}

std::vector<DetectedGame> GameScanner::scan_all() {
    std::vector<DetectedGame> games;
    if (games_folder_.empty()) return games;

    auto entries = platform::list_directory(games_folder_);
    int total = (int)entries.size();
    int idx = 0;

    for (auto& entry : entries) {
        std::string full_path = games_folder_ + "\\" + entry;
        if (!platform::is_directory(full_path)) continue;
        if (progress_callback_) progress_callback_(entry, total, idx++);

        auto game = scan_single(full_path);
        if (game.engine != EngineType::UNKNOWN) {
            game_cache_[game.folder_name] = game;
            games.push_back(game);
        }
    }
    return games;
}

DetectedGame GameScanner::scan_single(const std::string& folder_path) {
    DetectedGame game;
    game.folder_name = platform::get_filename(folder_path);
    game.folder_path = folder_path;

    if (check_krkr(folder_path, game) || check_renpy(folder_path, game) ||
        check_nscripter(folder_path, game) || check_yuris(folder_path, game) ||
        check_reallive(folder_path, game)) {
        find_save_files(folder_path, game);
        find_data_files(folder_path, game);
    }
    return game;
}

EngineType GameScanner::detect_engine(const std::string& folder_path) {
    DetectedGame game;
    game.folder_path = folder_path;
    if (check_krkr(folder_path, game)) return EngineType::KRKR;
    if (check_renpy(folder_path, game)) return EngineType::RENPY;
    if (check_nscripter(folder_path, game)) return EngineType::NSCRIPTER;
    if (check_yuris(folder_path, game)) return EngineType::YU_RIS;
    if (check_reallive(folder_path, game)) return EngineType::REALLIVE;
    return EngineType::UNKNOWN;
}

bool GameScanner::check_krkr(const std::string& path, DetectedGame& game) {
    std::vector<std::string> exes = { "krkr.exe", "kirikiri.exe", "kirikiri2.exe",
                                       "krkr_z.exe", "start.exe", "ran.exe" };
    for (auto& exe : exes) {
        if (platform::file_exists(path + "\\" + exe)) {
            game.engine = EngineType::KRKR; game.engine_str = "KRKR";
            game.exe_path = path + "\\" + exe;
            return true;
        }
    }
    auto files = platform::list_directory(path);
    for (auto& f : files) {
        std::string ext = to_lower(f).substr(to_lower(f).find_last_of('.') + 1);
        if (ext == "xp3" || ext == "tjs" || ext == "ks") {
            game.engine = EngineType::KRKR; game.engine_str = "KRKR";
            return true;
        }
    }
    if (platform::file_exists(path + "\\system\\Initialize.tjs")) {
        game.engine = EngineType::KRKR; game.engine_str = "KRKR";
        return true;
    }
    return false;
}

bool GameScanner::check_renpy(const std::string& path, DetectedGame& game) {
    auto files = platform::list_directory(path);
    for (auto& f : files) {
        std::string lower = to_lower(f);
        if (lower == "renpy" || ends_with(f, ".rpyc")) {
            game.engine = EngineType::RENPY; game.engine_str = "Ren'Py";
            return true;
        }
    }
    if (platform::is_directory(path + "\\renpy")) {
        game.engine = EngineType::RENPY; game.engine_str = "Ren'Py";
        return true;
    }
    return false;
}

bool GameScanner::check_nscripter(const std::string& path, DetectedGame& game) {
    std::vector<std::string> exes = { "nscripter.exe", "onscripter.exe", "ons.exe" };
    for (auto& exe : exes) {
        if (platform::file_exists(path + "\\" + exe)) {
            game.engine = EngineType::NSCRIPTER; game.engine_str = "NScripter";
            game.exe_path = path + "\\" + exe;
            return true;
        }
    }
    auto files = platform::list_directory(path);
    for (auto& f : files) {
        std::string ext = to_lower(f).substr(to_lower(f).find_last_of('.') + 1);
        if (ext == "nsa" || ext == "ns2" || ext == "ns3") {
            game.engine = EngineType::NSCRIPTER; game.engine_str = "NScripter";
            return true;
        }
    }
    return false;
}

bool GameScanner::check_yuris(const std::string& path, DetectedGame& game) {
    if (platform::file_exists(path + "\\yuris.exe")) {
        game.engine = EngineType::YU_RIS; game.engine_str = "YU-RIS";
        game.exe_path = path + "\\yuris.exe";
        return true;
    }
    auto files = platform::list_directory(path);
    for (auto& f : files)
        if (ends_with(f, ".yur")) { game.engine = EngineType::YU_RIS; game.engine_str = "YU-RIS"; return true; }
    return false;
}

bool GameScanner::check_reallive(const std::string& path, DetectedGame& game) {
    if (platform::file_exists(path + "\\reallive.exe")) {
        game.engine = EngineType::REALLIVE; game.engine_str = "Reallive";
        return true;
    }
    auto files = platform::list_directory(path);
    for (auto& f : files)
        if (ends_with(f, ".pac")) { game.engine = EngineType::REALLIVE; game.engine_str = "Reallive"; return true; }
    return false;
}

void GameScanner::find_save_files(const std::string& path, DetectedGame& game) {
    for (auto& dir : {"save", "savedata", "SaveData", "SAVE"}) {
        std::string full = path + "\\" + dir;
        if (platform::is_directory(full)) game.save_paths.push_back(full);
    }
    auto files = platform::list_directory(path);
    for (auto& f : files) {
        std::string ext = to_lower(f).substr(to_lower(f).find_last_of('.') + 1);
        if (ext == "ksd" || ext == "sav" || ext == "dat")
            game.save_paths.push_back(path + "\\" + f);
    }
}

void GameScanner::find_data_files(const std::string& path, DetectedGame& game) {
    for (auto& dir : {"bgimage", "fgimage", "image", "sound", "bgm", "se", "voice", "scenario", "data"}) {
        std::string full = path + "\\" + dir;
        if (platform::is_directory(full)) game.data_files.push_back(full);
    }
}

const std::unordered_map<std::string, DetectedGame>& GameScanner::get_cached_games() const { return game_cache_; }

}  // namespace krkr_sync
