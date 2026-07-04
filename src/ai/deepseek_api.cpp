#include "krkr_sync/deepseek_api.h"
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace krkr_sync {

void DeepSeekAPI::set_api_key(const std::string& key) { api_key_ = key; }

std::string DeepSeekAPI::build_prompt(const DetectedGame& game) {
    std::string prompt = "Identify this visual novel / galgame:\n\n";
    prompt += "Engine: " + game.engine_str + "\n";
    prompt += "Folder: " + game.folder_name + "\n";
    if (!game.exe_path.empty()) prompt += "Executable: " + game.exe_path + "\n";
    prompt += "\nRespond with JSON: {\"success\":true,\"game_name\":\"...\",\"engine\":\"...\",\"developer\":\"...\",\"year\":\"...\",\"description\":\"...\"}";
    return prompt;
}

std::string DeepSeekAPI::call_api(const std::string& prompt) {
    if (api_key_.empty()) return R"({"success":false,"error":"API key not set"})";

    HINTERNET hSession = WinHttpOpen(L"KRKR Sync/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return R"({"success":false,"error":"WinHttpOpen failed"})";

    HINTERNET hConnect = WinHttpConnect(hSession, L"api.deepseek.com",
                                         INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return R"({"success":false,"error":"Connect failed"})"; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/chat/completions",
                                             NULL, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return R"({"success":false,"error":"OpenRequest failed"})"; }

    // Build JSON request
    std::string json_body = R"({"model":")" + model_ + R"(","messages":[{"role":"system","content":"You are a visual novel identification expert. Respond ONLY with JSON."},{"role":"user","content":")" + prompt + R"("}],"temperature":0.1,"max_tokens":512})";

    // Set headers
    std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer ";
    headers += std::wstring(api_key_.begin(), api_key_.end());

    BOOL result = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.size(),
                                      (LPVOID)json_body.c_str(), (DWORD)json_body.size(),
                                      (DWORD)json_body.size(), 0);

    std::string response;
    if (result && WinHttpReceiveResponse(hRequest, NULL)) {
        char buf[4096];
        DWORD bytes_read = 0;
        while (WinHttpReadData(hRequest, buf, sizeof(buf) - 1, &bytes_read) && bytes_read > 0) {
            buf[bytes_read] = '\0';
            response += buf;
            bytes_read = 0;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return response.empty() ? R"({"success":false,"error":"Empty response"})" : response;
}

IdentifyResult DeepSeekAPI::identify_by_files(const DetectedGame& game) {
    return parse_response(call_api(build_prompt(game)));
}

IdentifyResult DeepSeekAPI::identify_by_name(const std::string& name) {
    std::string prompt = "Identify this visual novel: \"" + name + "\"\n"
                         "Respond with JSON: {\"success\":true,\"game_name\":\"...\",\"engine\":\"...\",\"developer\":\"...\",\"year\":\"...\"}";
    return parse_response(call_api(prompt));
}

IdentifyResult DeepSeekAPI::parse_response(const std::string& response) {
    IdentifyResult result;
    result.raw_response = response;

    // Simple JSON parsing without external library
    auto find_value = [&](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\"";
        auto pos = response.find(search);
        if (pos == std::string::npos) return "";
        pos = response.find(':', pos + search.size());
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < response.size() && response[pos] == ' ') pos++;
        if (pos >= response.size()) return "";
        if (response[pos] == '"') {
            pos++;
            auto end = response.find('"', pos);
            if (end == std::string::npos) return "";
            return response.substr(pos, end - pos);
        }
        auto end = response.find_first_of(",}", pos);
        if (end == std::string::npos) return response.substr(pos);
        return response.substr(pos, end - pos);
    };

    std::string success = find_value("success");
    result.success = (success == "true" || success == "1");
    result.game_name = find_value("game_name");
    result.engine = find_value("engine");
    result.developer = find_value("developer");
    result.year = find_value("year");
    result.description = find_value("description");

    return result;
}

}  // namespace krkr_sync
