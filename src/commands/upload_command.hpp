#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include "storage/upload_history.hpp"
#include <iostream>

namespace mstorage::commands {

class UploadCommand : public Command {
public:
    UploadCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager, storage::HistoryManager& history_manager, std::string file_path)
        : moodle_client_(moodle_client), session_manager_(session_manager), history_manager_(history_manager), file_path_(std::move(file_path)) {}

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        std::cout << "Fetching draft info...\n";
        auto draft_info = moodle_client_.get_draft_info(session->cookie);
        if (!draft_info) return std::unexpected(draft_info.error());

        std::cout << "Uploading file: " << file_path_ << "\n";
        auto upload_result = moodle_client_.upload_file(file_path_, *draft_info, session->cookie);
        if (!upload_result) return std::unexpected(upload_result.error());

        std::cout << "Committing changes...\n";
        auto commit_result = moodle_client_.commit_draft(*draft_info, session->cookie);
        if (!commit_result) return std::unexpected(commit_result.error());

        std::cout << "File uploaded successfully!\n";
        
        // Record in history
        auto history_result = history_manager_.record_upload(std::filesystem::path(file_path_).filename().string(), session->moodle_url);
        if (!history_result) {
            spdlog::warn("Failed to record upload in history: {}", history_result.error().message());
        }
        
        return {};
    }

private:
    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
    storage::HistoryManager& history_manager_;
    std::string file_path_;
};

} // namespace mstorage::commands
