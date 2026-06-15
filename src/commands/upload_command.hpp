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
                  std::vector<std::string> file_paths,
                  std::string remote_path = "/",
                  bool recursive = false)
        : moodle_client_(moodle_client), 
          session_manager_(session_manager), 
          history_manager_(history_manager),
          file_paths_(std::move(file_paths)),
          remote_path_(std::move(remote_path)),
          recursive_(recursive) {
        
        // Normalize remote path: ensure starts and ends with /
        if (remote_path_.empty() || remote_path_[0] != '/') remote_path_ = "/" + remote_path_;
        if (remote_path_.back() != '/') remote_path_ += "/";
    }

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        std::vector<std::pair<std::string, std::string>> files_to_upload; // local_path, target_remote_path

        for (const auto& path_str : file_paths_) {
            std::filesystem::path p(path_str);
            if (!std::filesystem::exists(p)) {
                std::cerr << "Warning: File/folder not found, skipping: " << path_str << "\n";
                continue;
            }

            if (std::filesystem::is_directory(p)) {
                if (!recursive_) {
                    std::cerr << "Warning: " << path_str << " is a directory. Use -r to upload recursively. Skipping.\n";
                    continue;
                }
                
                // Add base directory name to remote path for items inside it
                std::string base_dir_name = p.filename().string();
                std::string nested_base_remote = remote_path_ + base_dir_name + "/";

                for (auto const& dir_entry : std::filesystem::recursive_directory_iterator(p)) {
                    if (dir_entry.is_regular_file()) {
                        std::string relative = std::filesystem::relative(dir_entry.path(), p).string();
                        std::filesystem::path rel_path(relative);
                        
                        std::string specific_remote = nested_base_remote;
                        if (rel_path.has_parent_path()) {
                            specific_remote += rel_path.parent_path().string() + "/";
                        }
                        
                        // Fix windows backslashes if any (though we are on linux)
                        std::replace(specific_remote.begin(), specific_remote.end(), '\\', '/');

                        files_to_upload.push_back({dir_entry.path().string(), specific_remote});
                    }
                }
            } else {
                files_to_upload.push_back({p.string(), remote_path_});
            }
        }

        if (files_to_upload.empty()) {
            std::cerr << "No valid files to upload.\n";
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }

        std::cout << "Fetching draft info...\n";
        auto draft_info = moodle_client_.get_draft_info(session->cookie);
        if (!draft_info) return std::unexpected(draft_info.error());

        for (const auto& [local, target_remote] : files_to_upload) {
            if (target_remote != "/") {
                auto ensure_res = ensure_path_exists(target_remote, *draft_info, session->cookie);
                if (!ensure_res) spdlog::debug("Warning ensuring path: {}", ensure_res.error().message());
            }

            std::cout << "Uploading: " << local << " -> " << target_remote << "\n";
            auto upload_result = moodle_client_.upload_file(local, target_remote, *draft_info, session->cookie);
            if (!upload_result) return std::unexpected(upload_result.error());

            auto filename = std::filesystem::path(local).filename().string();
            auto rec_res = history_manager_.record_upload(filename, "moodle://" + target_remote + filename);
            if (!rec_res) {
                spdlog::warn("Failed to record upload history for {}: {}", filename, rec_res.error().message());
            }
        }

        std::cout << "Committing " << files_to_upload.size() << " file(s)...\n";
        auto commit_result = moodle_client_.commit_draft(*draft_info, session->cookie);
        if (!commit_result) return std::unexpected(commit_result.error());

        std::cout << "Upload completed successfully!\n";
        return {};
    }

private:
    std::expected<void, std::error_code> ensure_path_exists(const std::string& target_remote, const moodle::MoodleClient::DraftInfo& info, const std::string& cookie) {
        std::vector<std::string> parts;
        std::stringstream ss(target_remote);
        std::string item;
        while (std::getline(ss, item, '/')) {
            if (!item.empty()) parts.push_back(item);
        }

        std::string current_path = "/";
        for (const auto& part : parts) {
            auto res = moodle_client_.create_folder(part, current_path, info, cookie);
            current_path += part + "/";
        }
        return {};
    }

    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
    storage::HistoryManager& history_manager_;
    std::vector<std::string> file_paths_;
    std::string remote_path_;
    bool recursive_;
};

} // namespace mstorage::commands
