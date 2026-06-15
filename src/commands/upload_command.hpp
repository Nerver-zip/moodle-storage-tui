#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include "storage/upload_history.hpp"
#include <iostream>
#include <filesystem>
#include <vector>
#include <sstream>

namespace mstorage::commands {

class UploadCommand : public Command {
public:
    UploadCommand(moodle::MoodleClient& moodle_client, 
                  core::SessionManager& session_manager,
                  storage::HistoryManager& history_manager,
                  std::string file_path,
                  std::string remote_path = "/")
        : moodle_client_(moodle_client), 
          session_manager_(session_manager), 
          history_manager_(history_manager),
          file_path_(std::move(file_path)),
          remote_path_(std::move(remote_path)) {
        
        // Normalize remote path: ensure starts and ends with /
        if (remote_path_.empty() || remote_path_[0] != '/') {
            remote_path_ = "/" + remote_path_;
        }
        if (remote_path_.back() != '/') {
            remote_path_ += "/";
        }
    }

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        if (!std::filesystem::exists(file_path_)) {
            std::cerr << "File not found: " << file_path_ << "\n";
            return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        }

        std::cout << "Fetching draft info...\n";
        auto draft_info = moodle_client_.get_draft_info(session->cookie);
        if (!draft_info) return std::unexpected(draft_info.error());

        if (remote_path_ != "/") {
            std::cout << "Ensuring remote path exists: " << remote_path_ << "\n";
            auto ensure_res = ensure_path_exists(*draft_info, session->cookie);
            if (!ensure_res) {
                spdlog::warn("Some errors occurred while creating path, continuing with upload...");
            }
        }

        std::cout << "Uploading file: " << file_path_ << " to " << remote_path_ << "\n";
        auto upload_result = moodle_client_.upload_file(file_path_, remote_path_, *draft_info, session->cookie);
        if (!upload_result) return std::unexpected(upload_result.error());

        std::cout << "Committing changes...\n";
        auto commit_result = moodle_client_.commit_draft(*draft_info, session->cookie);
        if (!commit_result) return std::unexpected(commit_result.error());

        std::cout << "File uploaded successfully!\n";

        // Record history
        auto filename = std::filesystem::path(file_path_).filename().string();
        history_manager_.record_upload(filename, "moodle://" + remote_path_ + filename);

        return {};
    }

private:
    std::expected<void, std::error_code> ensure_path_exists(const moodle::MoodleClient::DraftInfo& info, const std::string& cookie) {
        std::vector<std::string> parts;
        std::stringstream ss(remote_path_);
        std::string item;
        while (std::getline(ss, item, '/')) {
            if (!item.empty()) parts.push_back(item);
        }

        std::string current_path = "/";
        for (const auto& part : parts) {
            spdlog::debug("Checking/Creating folder: {} in {}", part, current_path);
            auto res = moodle_client_.create_folder(part, current_path, info, cookie);
            if (!res) {
                // We ignore errors here because "folder already exists" is common
                spdlog::debug("Mkdir for {} failed (might already exist): {}", part, res.error().message());
            }
            current_path += part + "/";
        }
        return {};
    }

    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
    storage::HistoryManager& history_manager_;
    std::string file_path_;
    std::string remote_path_;
};

} // namespace mstorage::commands
