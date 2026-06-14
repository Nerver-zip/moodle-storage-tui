#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include <iostream>

namespace mstorage::commands {

class DeleteCommand : public Command {
public:
    DeleteCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager, std::string filename)
        : moodle_client_(moodle_client), session_manager_(session_manager), filename_(std::move(filename)) {}

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        std::cout << "Fetching draft info...\n";
        auto draft_info = moodle_client_.get_draft_info(session->cookie);
        if (!draft_info) return std::unexpected(draft_info.error());

        std::cout << "Deleting file: " << filename_ << "\n";
        auto delete_result = moodle_client_.delete_file(filename_, *draft_info, session->cookie);
        if (!delete_result) return std::unexpected(delete_result.error());

        std::cout << "Committing changes...\n";
        auto commit_result = moodle_client_.commit_draft(*draft_info, session->cookie);
        if (!commit_result) return std::unexpected(commit_result.error());

        std::cout << "File deleted successfully!\n";
        return {};
    }

private:
    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
    std::string filename_;
};

} // namespace mstorage::commands
