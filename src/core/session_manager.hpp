#pragma once
#include "models/models.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <expected>
#include <system_error>
#include <fstream>
#include <filesystem>

namespace mstorage::core {

class SessionManager {
public:
    SessionManager() {
        config_path_ = std::filesystem::path(getenv("HOME")) / ".config" / "mstorage";
        std::filesystem::create_directories(config_path_);
    }

    std::expected<void, std::error_code> save(const models::SessionData& data) {
        nlohmann::json j;
        j["moodle_url"] = data.moodle_url;
        j["sesskey"] = data.sesskey;
        j["cookie"] = data.cookie;

        auto file_path = config_path_ / "session.json";
        std::ofstream file(file_path);
        if (!file.is_open()) return std::unexpected(std::make_error_code(std::errc::io_error));
        file << j.dump(4);
        file.close();

        // Secure permissions: 0600 (User Read/Write only)
        std::error_code ec;
        std::filesystem::permissions(file_path, 
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, ec);
        
        if (ec) {
            spdlog::warn("Failed to set secure permissions on session.json: {}", ec.message());
        }

        return {};
    }

    std::expected<models::SessionData, std::error_code> load() {
        std::ifstream file(config_path_ / "session.json");
        if (!file.is_open()) return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));

        nlohmann::json j;
        file >> j;

        return models::SessionData{
            j["moodle_url"],
            j["sesskey"],
            j["cookie"]
        };
    }

private:
    std::filesystem::path config_path_;
};

} // namespace mstorage::core
