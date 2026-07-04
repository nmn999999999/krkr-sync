#include "krkr_sync/deepseek_api.h"

#ifdef _WIN32
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

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

#ifdef _WIN32
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

    std::string json_body = R"({"model":")" + model_ + R"(","messages":[{"role":"system","content":"You are a visual novel identification expert. Respond ONLY with JSON."},{"role":"user","content":")" + prompt + R"("}],"temperature":0.1,"max_tokens":512})";

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
#else
std::string DeepSeekAPI::call_api(const std::string& prompt) {
    if (api_key_.empty()) return R"({"success":false,"error":"API key not set"})";

    // Simple HTTPS POST using POSIX sockets + OpenSSL
    struct addrinfo hints{}, *result;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo("api.deepseek.com", "443", &hints, &result) != 0)
        return R"({"success":false,"error":"DNS resolution failed"})";

    int sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock < 0) { freeaddrinfo(result); return R"({"success":false,"error":"Socket creation failed"})"; }

    if (connect(sock, result->ai_addr, result->ai_addrlen) < 0) {
        close(sock); freeaddrinfo(result);
        return R"({"success":false,"error":"Connection failed"})";
    }
    freeaddrinfo(result);

    // SSL
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
        return R"({"success":false,"error":"SSL handshake failed"})";
    }

    std::string json_body = R"({"model":")" + model_ + R"(","messages":[{"role":"system","content":"You are a visual novel identification expert. Respond ONLY with JSON."},{"role":"user","content":")" + prompt + R"("}],"temperature":0.1,"max_tokens":512})";

    std::string request = "POST /chat/completions HTTP/1.1\r\n"
                          "Host: api.deepseek.com\r\n"
                          "Content-Type: application/json\r\n"
                          "Authorization: Bearer " + api_key_ + "\r\n"
                          "Content-Length: " + std::to_string(json_body.size()) + "\r\n"
                          "Connection: close\r\n"
                          "\r\n" + json_body;

    SSL_write(ssl, request.c_str(), (int)request.size());

    std::string response;
    char buf[4096];
    int n;
    while ((n = SSL_read(ssl, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        response += buf;
    }

    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(sock);

    // Extract body (after double CRLF)
    auto pos = response.find("\r\n\r\n");
    if (pos != std::string::npos) response = response.substr(pos + 4);

    return response.empty() ? R"({"success":false,"error":"Empty response"})" : response;
}
#endif

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
