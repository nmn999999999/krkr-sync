#include "krkr_sync/sync_engine.h"
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
    std::string deepseek_key;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--folder" && i + 1 < argc) config.games_folder = argv[++i];
        else if (arg == "--port" && i + 1 < argc) config.sync_port = (uint16_t)std::stoi(argv[++i]);
        else if (arg == "--name" && i + 1 < argc) config.device_name = argv[++i];
        else if (arg == "--no-discover") config.auto_discover = false;
        else if (arg == "--help") {
            std::cout << "KRKR Sync Server\nUsage: " << argv[0] << " --folder <path> [--port 29526] [--name <name>]\n";
            return 0;
        }
    }

    if (config.games_folder.empty()) { std::cerr << "Error: --folder required\n"; return 1; }
    if (config.device_name.empty()) config.device_name = platform::get_device_name();

    std::cout << "=== KRKR Sync Server ===\nDevice: " << config.device_name
              << "\nGames: " << config.games_folder << "\nPort: " << config.sync_port << "\n\n";

    SyncEngine engine(config);
    engine.set_status_callback([](const std::string& m) { std::cout << "[STATUS] " << m << "\n"; });
    engine.set_progress_callback([](const std::string& g, int p) { std::cout << "\r[SCAN] " << g << " (" << p << "%)" << std::flush; });

    if (!engine.start()) { std::cerr << "Failed to start\n"; return 1; }
    std::cout << "\nServer running. Ctrl+C to stop.\n\n";

    while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop();
    std::cout << "Done.\n";
    return 0;
}
