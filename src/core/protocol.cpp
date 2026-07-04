#include "krkr_sync/protocol.h"

namespace krkr_sync {

std::vector<uint8_t> serialize_string(const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    std::vector<uint8_t> result(sizeof(uint32_t) + len);
    std::memcpy(result.data(), &len, sizeof(uint32_t));
    std::memcpy(result.data() + sizeof(uint32_t), s.data(), len);
    return result;
}

std::string deserialize_string(const uint8_t* data, size_t len, size_t& offset) {
    if (offset + sizeof(uint32_t) > len) return "";
    uint32_t str_len = 0;
    std::memcpy(&str_len, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    if (offset + str_len > len) return "";
    std::string result(reinterpret_cast<const char*>(data + offset), str_len);
    offset += str_len;
    return result;
}

std::vector<uint8_t> serialize_u64(uint64_t v) {
    std::vector<uint8_t> r(8);
    std::memcpy(r.data(), &v, 8);
    return r;
}

uint64_t deserialize_u64(const uint8_t* data, size_t& offset) {
    uint64_t v = 0;
    std::memcpy(&v, data + offset, 8);
    offset += 8;
    return v;
}

std::vector<uint8_t> GameInfo::serialize() const {
    std::vector<uint8_t> result;
    auto append = [&](const std::vector<uint8_t>& v) {
        result.insert(result.end(), v.begin(), v.end());
    };
    append(serialize_string(id));
    append(serialize_string(name));
    append(serialize_string(engine));
    append(serialize_string(folder_path));
    append(serialize_u64(last_modified));
    append(serialize_u64(total_size));
    return result;
}

GameInfo GameInfo::deserialize(const std::vector<uint8_t>& data) {
    GameInfo info;
    size_t offset = 0;
    info.id = deserialize_string(data.data(), data.size(), offset);
    info.name = deserialize_string(data.data(), data.size(), offset);
    info.engine = deserialize_string(data.data(), data.size(), offset);
    info.folder_path = deserialize_string(data.data(), data.size(), offset);
    info.last_modified = deserialize_u64(data.data(), offset);
    info.total_size = deserialize_u64(data.data(), offset);
    return info;
}

std::vector<uint8_t> SaveEntry::serialize() const {
    std::vector<uint8_t> result;
    auto append = [&](const std::vector<uint8_t>& v) {
        result.insert(result.end(), v.begin(), v.end());
    };
    append(serialize_string(game_id));
    append(serialize_string(relative_path));
    append(serialize_u64(file_size));
    append(serialize_u64(modified_time));
    result.insert(result.end(), md5.begin(), md5.end());
    return result;
}

SaveEntry SaveEntry::deserialize(const std::vector<uint8_t>& data) {
    SaveEntry entry;
    size_t offset = 0;
    entry.game_id = deserialize_string(data.data(), data.size(), offset);
    entry.relative_path = deserialize_string(data.data(), data.size(), offset);
    entry.file_size = deserialize_u64(data.data(), offset);
    entry.modified_time = deserialize_u64(data.data(), offset);
    std::memcpy(entry.md5.data(), data.data() + offset, 16);
    return entry;
}

}  // namespace krkr_sync
