#pragma once

#include "krkr_sync/protocol.h"
#include "krkr_sync/game_scanner.h"

namespace krkr_sync {

struct IdentifyResult {
    bool success = false;
    std::string game_name, engine, developer, year, description, raw_response;
};

class DeepSeekAPI {
public:
    void set_api_key(const std::string& key);
    IdentifyResult identify_by_files(const DetectedGame& game);
    IdentifyResult identify_by_name(const std::string& name);

private:
    std::string call_api(const std::string& prompt);
    std::string build_prompt(const DetectedGame& game);
    IdentifyResult parse_response(const std::string& response);

    std::string api_key_;
    std::string model_ = "deepseek-chat";
};

}  // namespace krkr_sync
