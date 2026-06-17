#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include <iostream>
#include <fstream>

namespace mstorage::commands {

class MkdirCommand : public Command {
public:
    MkdirCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager, std::string folder_name)
        : moodle_client_(moodle_client), session_manager_(session_manager), folder_name_(std::move(folder_name)) {}

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        std::cout << "Fetching draft info...\n";
        auto draft_info = moodle_client_.get_draft_info(session->web_cookie);
        if (!draft_info) return std::unexpected(draft_info.error());

        std::cout << "Creating folder: " << folder_name_ << "\n";
        auto res = moodle_client_.create_folder(folder_name_, "/", *draft_info, session->web_cookie);
        if (!res) return std::unexpected(res.error());

        std::cout << "Committing changes...\n";
        return moodle_client_.commit_draft(*draft_info, session->web_cookie);
    }

private:
    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
    std::string folder_name_;
};

} // namespace mstorage::commands
