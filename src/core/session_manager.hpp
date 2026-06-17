#pragma once
#include "models/models.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <libsecret/secret.h>
#include <expected>
#include <system_error>
#include <fstream>
#include <filesystem>

namespace mstorage::core {

class SessionManager {
public:
    SessionManager() {
        char* home = getenv("HOME");
        config_path_ = std::filesystem::path(home ? home : ".") / ".config" / "mstorage";
        std::filesystem::create_directories(config_path_);
    }

    std::expected<void, std::error_code> save(const models::SessionData& data, const std::string& username = "", const std::string& password = "") {
        nlohmann::json j;
        j["moodle_url"] = data.moodle_url;
        if (!username.empty()) j["username"] = username;

        auto file_path = config_path_ / "session.json";
        std::ofstream file(file_path);
        if (!file.is_open()) return std::unexpected(std::make_error_code(std::errc::io_error));
        file << j.dump(4);
        file.close();

        GError* error = nullptr;

        // Save wstoken
        if (!data.wstoken.empty()) {
            secret_password_store_sync(
                get_schema(), SECRET_COLLECTION_DEFAULT, "Moodle Storage Token",
                data.wstoken.c_str(), nullptr, &error,
                "moodle_url", data.moodle_url.c_str(), "type", "wstoken", nullptr
            );
        }

        // Save web_cookie
        if (!data.web_cookie.empty()) {
            secret_password_store_sync(
                get_schema(), SECRET_COLLECTION_DEFAULT, "Moodle Storage Cookie",
                data.web_cookie.c_str(), nullptr, &error,
                "moodle_url", data.moodle_url.c_str(), "type", "web_cookie", nullptr
            );
        }

        // Save Credentials
        if (!username.empty() && !password.empty()) {
            secret_password_store_sync(
                get_schema(), SECRET_COLLECTION_DEFAULT, "Moodle Storage Credentials",
                password.c_str(), nullptr, &error,
                "moodle_url", data.moodle_url.c_str(), "username", username.c_str(), "type", "credentials", nullptr
            );
        }

        if (error) {
            spdlog::error("Keyring storage failed: {}", error->message);
            g_error_free(error);
            return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
        }

        return {};
    }

    struct FullCredentials {
        std::string username;
        std::string password;
    };

    std::expected<FullCredentials, std::error_code> load_credentials(const std::string& moodle_url) {
        GError* error = nullptr;
        char* password = secret_password_lookup_sync(
            get_schema(), nullptr, &error,
            "moodle_url", moodle_url.c_str(), "type", "credentials", nullptr
        );

        if (error || !password) {
            if (error) g_error_free(error);
            return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        }

        std::string final_pass(password);
        secret_password_free(password);
        
        std::ifstream file(config_path_ / "session.json");
        nlohmann::json j;
        file >> j;
        return FullCredentials{j.value("username", ""), final_pass};
    }

    std::expected<models::SessionData, std::error_code> load() {
        std::ifstream file(config_path_ / "session.json");
        if (!file.is_open()) return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));

        nlohmann::json j;
        try { file >> j; } catch (...) { return std::unexpected(std::make_error_code(std::errc::invalid_argument)); }

        std::string url = j["moodle_url"];
        models::SessionData data;
        data.moodle_url = url;

        GError* error = nullptr;
        char* wstoken = secret_password_lookup_sync(get_schema(), nullptr, &error, "moodle_url", url.c_str(), "type", "wstoken", nullptr);
        if (wstoken) {
            data.wstoken = wstoken;
            secret_password_free(wstoken);
        }

        char* web_cookie = secret_password_lookup_sync(get_schema(), nullptr, &error, "moodle_url", url.c_str(), "type", "web_cookie", nullptr);
        if (web_cookie) {
            data.web_cookie = web_cookie;
            secret_password_free(web_cookie);
        }

        return data;
    }

    std::expected<void, std::error_code> clear_credentials() {
        // Clear from Keyring
        GError* error = nullptr;
        
        // We read the URL to know what to delete
        std::string url = "";
        std::ifstream file(config_path_ / "session.json");
        if (file.is_open()) {
            nlohmann::json j;
            try { file >> j; url = j.value("moodle_url", ""); } catch(...) {}
        }

        if (!url.empty()) {
            secret_password_clear_sync(get_schema(), nullptr, &error, "moodle_url", url.c_str(), "type", "wstoken", nullptr);
            secret_password_clear_sync(get_schema(), nullptr, &error, "moodle_url", url.c_str(), "type", "web_cookie", nullptr);
            secret_password_clear_sync(get_schema(), nullptr, &error, "moodle_url", url.c_str(), "type", "credentials", nullptr);
            if (error) g_error_free(error);
        }

        // Delete session file
        std::error_code ec;
        std::filesystem::remove(config_path_ / "session.json", ec);
        if (ec) return std::unexpected(ec);

        return {};
    }

private:
    const SecretSchema* get_schema() {
        static const SecretSchema schema = {
            "org.mstorage.Generic", SECRET_SCHEMA_NONE,
            {
                { "moodle_url", SECRET_SCHEMA_ATTRIBUTE_STRING },
                { "type", SECRET_SCHEMA_ATTRIBUTE_STRING },
                { "username", SECRET_SCHEMA_ATTRIBUTE_STRING },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 },
                { nullptr, (SecretSchemaAttributeType)0 }
            }
        };
        return &schema;
    }

    std::filesystem::path config_path_;
};

} // namespace mstorage::core
