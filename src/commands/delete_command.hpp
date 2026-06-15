#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include <iostream>

namespace mstorage::commands {

class DeleteCommand : public Command {
public:
    DeleteCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager, std::string target_name, std::string remote_path = "/", bool recursive = false)
        : moodle_client_(moodle_client), session_manager_(session_manager), target_name_(std::move(target_name)), remote_path_(std::move(remote_path)), recursive_(recursive) {
        
        // Normalize path
        if (remote_path_.empty() || remote_path_[0] != '/') remote_path_ = "/" + remote_path_;
        if (remote_path_.back() != '/') remote_path_ += "/";
    }

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        if (recursive_ && target_name_.empty()) {
            std::cerr << "Error: Cannot delete the root directory.\n";
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        std::cout << "Fetching draft info...\n";
        auto draft_info = moodle_client_.get_draft_info(session->cookie);
        if (!draft_info) return std::unexpected(draft_info.error());

        std::cout << "Deleting " << (recursive_ ? "folder" : "file") << ": " << target_name_ << " from " << remote_path_ << "\n";
        
        auto delete_result = moodle_client_.delete_item(target_name_, remote_path_, recursive_, *draft_info, session->cookie);
        if (!delete_result) return std::unexpected(delete_result.error());

        std::cout << "Committing changes...\n";
        auto commit_result = moodle_client_.commit_draft(*draft_info, session->cookie);
        if (!commit_result) return std::unexpected(commit_result.error());

        std::cout << "Item deleted successfully!\n";
        return {};
    }

private:
    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
    std::string target_name_;
    std::string remote_path_;
    bool recursive_;
};

} // namespace mstorage::commands
