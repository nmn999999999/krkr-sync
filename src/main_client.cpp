#include "krkr_sync/sync_engine.h"
#include "krkr_sync/deepseek_api.h"
#include "krkr_sync/platform.h"
#include <csignal>

using namespace krkr_sync;
static std::atomic<bool> g_running{true};
static void signal_handler(int) { g_running = false; }

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    platform::init_sockets();

    SyncConfig config;
    std::string deepseek_key, identify_game;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--folder" && i + 1 < argc) config.games_folder = argv[++i];
        else if (arg == "--port" && i + 1 < argc) config.sync_port = (uint16_t)std::stoi(argv[++i]);
        else if (arg == "--name" && i + 1 < argc) config.device_name = argv[++i];
        else if (arg == "--deepseek-key" && i + 1 < argc) deepseek_key = argv[++i];
        else if (arg == "--identify" && i + 1 < argc) identify_game = argv[++i];
        else if (arg == "--help") {
            std::cout << "KRKR Sync Client\nUsage: " << argv[0] << " --folder <path> [--deepseek-key <key>] [--identify <game>]\n";
            return 0;
        }
    }

    if (config.games_folder.empty() && identify_game.empty()) { std::cerr << "Error: --folder required\n"; return 1; }
    if (config.device_name.empty()) config.device_name = platform::get_device_name();

    // Identify mode
    if (!identify_game.empty() && !deepseek_key.empty()) {
        DeepSeekAPI api;
        api.set_api_key(deepseek_key);
        GameScanner scanner;
        scanner.set_games_folder(config.games_folder);
        auto game = scanner.scan_single(config.games_folder + "\\" + identify_game);
        std::cout << "Identifying: " << identify_game << "\n";
        auto result = api.identify_by_files(game);
        if (result.success) {
            std::cout << "=== Identified ===\nName: " << result.game_name << "\nEngine: " << result.engine
                      << "\nDeveloper: " << result.developer << "\nYear: " << result.year << "\n";
        } else {
            std::cout << "Could not identify. Response: " << result.raw_response << "\n";
        }
        return 0;
    }

    std::cout << "=== KRKR Sync Client ===\nDevice: " << config.device_name
              << "\nGames: " << config.games_folder << "\n\n";

    SyncEngine engine(config);
    engine.set_status_callback([](const std::string& m) { std::cout << "[STATUS] " << m << "\n"; });
    engine.set_progress_callback([](const std::string& g, int p) { std::cout << "\r[SCAN] " << g << " (" << p << "%)" << std::flush; });

    if (!engine.start()) { std::cerr << "Failed to start\n"; return 1; }

    std::cout << "\nLocal games: " << engine.get_local_games().size() << "\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    auto peers = engine.get_discovered_peers();
    std::cout << "Peers found: " << peers.size() << "\n";
    for (auto& p : peers) std::cout << "  - " << p.device_name << " at " << p.ip_address << ":" << p.port << "\n";
    std::cout << "\nClient running. Ctrl+C to stop.\n\n";

    while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop();
    std::cout << "Done.\n";
    return 0;
}
