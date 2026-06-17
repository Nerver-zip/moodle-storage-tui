#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include "storage/upload_history.hpp"
#include <iostream>
#include <vector>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace mstorage::commands {

class UploadCommand : public Command {
public:
    UploadCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager, 
                  storage::HistoryManager& history_manager, std::vector<std::string> file_paths, 
                  std::string remote_path, bool recursive)
        : moodle_client_(moodle_client), session_manager_(session_manager), 
          history_manager_(history_manager), file_paths_(std::move(file_paths)), 
          remote_path_(std::move(remote_path)), recursive_(recursive) {}

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        std::cout << "Fetching draft info...\n";
        auto draft_info = moodle_client_.get_draft_info();
        if (!draft_info) return std::unexpected(draft_info.error());

        for (const auto& path : file_paths_) {
            if (!std::filesystem::exists(path)) {
                std::cerr << "Warning: File/folder not found, skipping: " << path << "\n";
                continue;
            }

            if (std::filesystem::is_directory(path)) {
                if (recursive_) {
                    auto res = upload_recursive(path, remote_path_, *draft_info);
                    if (!res) return std::unexpected(res.error());
                } else {
                    std::cerr << "Warning: Skipping directory (use -r to upload recursive): " << path << "\n";
                }
            } else {
                auto res = upload_single_file(path, remote_path_, *draft_info);
                if (!res) return std::unexpected(res.error());
            }
        }

        std::cout << "Committing " << uploaded_count_ << " file(s)...\n";
        auto commit_res = moodle_client_.commit_draft(*draft_info);
        if (commit_res) {
            std::cout << "Upload completed successfully!\n";
        }
        return commit_res;
    }

private:
    std::expected<void, std::error_code> upload_single_file(const std::string& local_path, const std::string& remote_dir, const moodle::MoodleClient::DraftInfo& info) {
        std::cout << "Uploading: " << local_path << " -> " << remote_dir << "\n";
        auto res = moodle_client_.upload_file(local_path, remote_dir, info);
        if (res) {
            uploaded_count_++;
            (void)history_manager_.record_upload(std::filesystem::path(local_path).filename().string(), "REST_API");
        }
        return res;
    }

    std::expected<void, std::error_code> upload_recursive(const std::filesystem::path& local_dir, const std::string& remote_parent, const moodle::MoodleClient::DraftInfo& info) {
        std::string folder_name = local_dir.filename().string();
        std::string remote_dir = remote_parent + folder_name + "/";

        for (const auto& entry : std::filesystem::directory_iterator(local_dir)) {
            if (entry.is_directory()) {
                auto res = upload_recursive(entry.path(), remote_dir, info);
                if (!res) return std::unexpected(res.error());
            } else {
                auto res = upload_single_file(entry.path().string(), remote_dir, info);
                if (!res) return std::unexpected(res.error());
            }
        }
        return {};
    }

    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
    storage::HistoryManager& history_manager_;
    std::vector<std::string> file_paths_;
    std::string remote_path_;
    bool recursive_;
    int uploaded_count_ = 0;
};

} // namespace mstorage::commands
