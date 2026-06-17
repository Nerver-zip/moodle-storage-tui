#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <expected>
#include <system_error>
#include <filesystem>
#include <spdlog/spdlog.h>
#include "models/models.hpp"

namespace mstorage::storage {

class HistoryManager {
public:
    HistoryManager(std::string db_path = "") {
        if (db_path.empty()) {
            auto home = getenv("HOME");
            auto config_path = std::filesystem::path(home ? home : ".") / ".config" / "mstorage";
            std::filesystem::create_directories(config_path);
            db_path_ = (config_path / "uploads.db").string();
        } else {
            db_path_ = std::move(db_path);
        }
        init_db();
    }

    ~HistoryManager() {
        if (db_) sqlite3_close(db_);
    }

    std::expected<void, std::error_code> record_upload(const std::string& filename, const std::string& url) {
        const char* sql = "INSERT INTO history (filename, url, timestamp) VALUES (?, ?, datetime('now'));";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }

        sqlite3_bind_text(stmt, 1, filename.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, url.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }

        sqlite3_finalize(stmt);
        return {};
    }

    struct HistoryEntry {
        std::string filename;
        std::string url;
        std::string timestamp;
    };

    std::expected<std::vector<HistoryEntry>, std::error_code> get_history(int limit = 10) {
        std::vector<HistoryEntry> history;
        const char* sql = "SELECT filename, url, timestamp FROM history ORDER BY timestamp DESC LIMIT ?;";
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }

        sqlite3_bind_int(stmt, 1, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            history.push_back({
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
            });
        }

        sqlite3_finalize(stmt);
        return history;
    }

    std::expected<void, std::error_code> clear() {
        const char* sql = "DELETE FROM history;";
        char* err_msg = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            spdlog::error("Failed to clear history: {}", err_msg);
            sqlite3_free(err_msg);
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }
        return {};
    }

private:
    void init_db() {
        if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
            spdlog::error("Failed to open database: {}", sqlite3_errmsg(db_));
            return;
        }

        const char* sql = "CREATE TABLE IF NOT EXISTS history ("
                          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                          "filename TEXT NOT NULL,"
                          "url TEXT NOT NULL,"
                          "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";
        
        char* err_msg = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            spdlog::error("Failed to create table: {}", err_msg);
            sqlite3_free(err_msg);
        }
    }

    sqlite3* db_ = nullptr;
    std::string db_path_;
};

} // namespace mstorage::storage
