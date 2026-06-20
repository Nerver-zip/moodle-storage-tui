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
                  std::vector<std::string> target_names, std::string remote_path)
        : moodle_client_(moodle_client), session_manager_(session_manager), 
          target_names_(std::move(target_names)), remote_path_(std::move(remote_path)) {}

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

        // Fetch parent directory files list to determine if targets are folders or files
        auto files = moodle_client_.list_files("", remote_path_);
        if (!files) return std::unexpected(files.error());

        std::cout << "Deleting " << target_names_.size() << " item(s) from " << remote_path_ << "\n";
        
        std::vector<models::DeleteItem> items;
        for (const auto& name : target_names_) {
             bool is_folder = false;
             auto it = std::find_if(files->begin(), files->end(), [&](const models::MoodleFile& f) {
                 return f.filename == "." && get_folder_name(f.filepath) == name;
             });
             if (it != files->end()) {
                 is_folder = true;
             }
             items.push_back({name, remote_path_, is_folder});
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
    std::string get_folder_name(const std::string& filepath) {
        if (filepath == "/") return "/";
        std::string clean_path = filepath;
        if (clean_path.back() == '/') clean_path.pop_back();
        auto pos = clean_path.find_last_of('/');
        if (pos != std::string::npos) return clean_path.substr(pos + 1);
        return clean_path;
    }

    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
    std::vector<std::string> target_names_;
    std::string remote_path_;
};

} // namespace mstorage::commands
