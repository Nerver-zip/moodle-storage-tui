#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include "core/session_helper.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace mstorage::commands {

class DeleteCommand : public Command {
public:
    DeleteCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager, 
                  std::vector<std::string> target_names, std::string remote_path, bool recursive)
        : moodle_client_(moodle_client), session_manager_(session_manager), 
          target_names_(std::move(target_names)), remote_path_(std::move(remote_path)), recursive_(recursive) {}

    std::expected<void, std::error_code> execute() override {
        auto draft_info = core::ensure_web_session(moodle_client_, session_manager_);
        if (!draft_info) {
            std::cerr << "Warning: Session expired or invalid. Falling back to REST (may fail deleting folders).\n";
            draft_info = moodle_client_.get_draft_info();
        }

        if (!draft_info) return std::unexpected(draft_info.error());

        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());
        std::string active_cookie = session->web_cookie;

        std::cout << "Deleting " << target_names_.size() << " item(s) from " << remote_path_ << "\n";
        
        std::vector<models::DeleteItem> items;
        for (const auto& name : target_names_) {
             items.push_back({name, remote_path_, recursive_});
        }
        
        auto delete_result = moodle_client_.delete_items(items, *draft_info, active_cookie);
        if (!delete_result) return std::unexpected(delete_result.error());

        std::cout << "Committing changes...\n";
        auto commit_result = moodle_client_.commit_draft(*draft_info, active_cookie);
        if (!commit_result) return std::unexpected(commit_result.error());

        std::cout << "Items deleted successfully!\n";
        return {};
    }

private:
    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
    std::vector<std::string> target_names_;
    std::string remote_path_;
    bool recursive_;
};

} // namespace mstorage::commands
