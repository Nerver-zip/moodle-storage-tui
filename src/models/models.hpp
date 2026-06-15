#pragma once
#include <string>
#include <filesystem>

namespace mstorage::models {

struct SessionData {
    std::string moodle_url;
    std::string sesskey;
    std::string cookie;
};

struct FileInfo {
    std::filesystem::path path;
    uintmax_t size;
};

struct UploadResult {
    std::string filename;
    std::string file_url;
    std::string itemid;
};

struct MoodleFile {
    std::string filename;
    std::string filepath;
    std::string url;
    uintmax_t size;
    std::string size_f;
    time_t datemodified;
};

struct StorageUsage {
    uintmax_t used_bytes;
    uintmax_t total_bytes;
};

struct DeleteItem {
    std::string name;
    std::string parent_path;
    bool is_folder;
};

} // namespace mstorage::models
