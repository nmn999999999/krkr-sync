#pragma once

#include "krkr_sync/protocol.h"

namespace krkr_sync {
namespace platform {

std::string get_device_name();
std::string get_data_dir();
std::string get_temp_dir();
std::string get_filename(const std::string& path);

bool create_directory_recursive(const std::string& path);
bool file_exists(const std::string& path);
bool is_directory(const std::string& path);
std::vector<std::string> list_directory(const std::string& path);

uint64_t get_file_size(const std::string& path);
uint64_t get_file_modified_time(const std::string& path);

std::vector<uint8_t> read_file(const std::string& path);
bool write_file(const std::string& path, const std::vector<uint8_t>& data);

std::vector<std::string> get_local_ips();

std::vector<uint8_t> compute_md5(const std::vector<uint8_t>& data);
std::vector<uint8_t> compute_file_md5(const std::string& filepath);

// Windows Sockets init/cleanup
void init_sockets();
void cleanup_sockets();

}  // namespace platform
}  // namespace krkr_sync
