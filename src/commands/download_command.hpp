#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include "core/session_helper.hpp"
#include <iostream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace mstorage::commands {

class DownloadCommand : public Command {
public:
    DownloadCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager, 
                    std::vector<std::string> filenames, bool use_zip = true)
        : moodle_client_(moodle_client), session_manager_(session_manager), 
          filenames_(std::move(filenames)), use_zip_(use_zip) {}

    std::expected<void, std::error_code> execute() override {
        auto draft_info = core::ensure_web_session(moodle_client_, session_manager_);
        if (!draft_info) {
            std::cerr << "Session expired or invalid. Please login again using 'mstorage login'.\n";
            return std::unexpected(draft_info.error());
        }

        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        std::cout << "Fetching file list...\n";
        auto files = moodle_client_.list_files();
        if (!files) return std::unexpected(files.error());

        for (const auto& target_name : filenames_) {
            std::string clean_target = target_name;
            if (clean_target.size() > 4 && clean_target.substr(clean_target.size() - 4) == ".zip") {
                clean_target = clean_target.substr(0, clean_target.size() - 4);
            }

            auto it = std::find_if(files->begin(), files->end(), [&](const models::MoodleFile& f) {
                return f.filename == target_name || 
                       (f.filename == "." && get_folder_name(f.filepath) == target_name) ||
                       (f.filename == "." && get_folder_name(f.filepath) == clean_target);
            });

            if (it == files->end()) {
                std::cerr << "File or folder not found on Moodle: " << target_name << "\n";
                continue;
            }

            if (it->filename == ".") {
                // Folder
                if (use_zip_ && draft_info) {
                    std::cout << "Attempting native ZIP compression for '" << target_name << "'...\n";
                    auto zip_url = moodle_client_.zip_folder(it->filepath, *draft_info, session->web_cookie);
                    if (zip_url) {
                        std::string output_zip = target_name;
                        if (output_zip.size() < 4 || output_zip.substr(output_zip.size() - 4) != ".zip") {
                            output_zip += ".zip";
                        }
                        std::cout << "Downloading ZIP: " << output_zip << "...\n";
                        (void)moodle_client_.download_file(*zip_url, output_zip, session->web_cookie);
                        continue;
                    }
                    std::cout << "Falling back to individual file downloads...\n";
                }
                
                download_recursive(it->filepath, it->filepath, session->web_cookie);
            } else {
                // File
                std::cout << "Downloading file: " << target_name << "...\n";
                (void)moodle_client_.download_file(file_url_with_token(it->url), target_name, session->web_cookie);
            }
        }

        return {};
    }

private:
    std::string file_url_with_token(const std::string& url) {
        return url;
    }

    void download_recursive(const std::string& remote_path, const std::string& base_remote_path, const std::string& cookie) {
        auto files = moodle_client_.list_files("", remote_path);
        if (!files) return;

        for (const auto& file : *files) {
            if (file.filename == ".") {
                if (file.filepath != remote_path) {
                    download_recursive(file.filepath, base_remote_path, cookie);
                }
            } else {
                std::string rel = file.filepath.substr(base_remote_path.length());
                std::filesystem::path local_dir = std::filesystem::path(get_folder_name(base_remote_path)) / rel;
                std::filesystem::create_directories(local_dir);
                
                std::filesystem::path local_path = local_dir / file.filename;
                std::cout << "  -> " << local_path.string() << "\n";
                (void)moodle_client_.download_file(file.url, local_path.string(), cookie);
            }
        }
    }

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
    std::vector<std::string> filenames_;
    bool use_zip_;
};

} // namespace mstorage::commands
