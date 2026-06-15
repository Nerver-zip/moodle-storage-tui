#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include <iostream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace mstorage::commands {

class DownloadCommand : public Command {
public:
    DownloadCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager, std::vector<std::string> filenames, bool recursive = false)
        : moodle_client_(moodle_client), session_manager_(session_manager), filenames_(std::move(filenames)), recursive_(recursive) {}

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        std::cout << "Fetching file list...\n";
        auto files = moodle_client_.list_files(session->cookie);
        if (!files) return std::unexpected(files.error());

        auto draft_info = moodle_client_.get_draft_info(session->cookie);
        if (!draft_info) return std::unexpected(draft_info.error());

        for (auto target_name : filenames_) {
            // Normalize target_name: remove trailing slash if any for matching
            if (target_name.length() > 1 && target_name.back() == '/') {
                target_name.pop_back();
            }

            spdlog::debug("Searching for target: {}", target_name);
            
            auto it = std::find_if(files->begin(), files->end(), [&](const models::MoodleFile& f) {
                if (f.filename == ".") {
                    std::string f_name = get_folder_name(f.filepath);
                    return f_name == target_name;
                }
                return f.filename == target_name;
            });

            if (it == files->end()) {
                std::cerr << "File or folder not found on Moodle: " << target_name << "\n";
                continue;
            }

            if (it->filename == ".") {
                // It's a folder
                if (!recursive_) {
                    std::cerr << "Error: '" << target_name << "' is a directory. Use -r to download as ZIP.\n";
                    continue;
                }
                
                std::cout << "Compressing folder '" << target_name << "' on server...\n";
                auto zip_url = moodle_client_.zip_folder(it->filepath, *draft_info, session->cookie);
                if (!zip_url) {
                    std::cerr << "Failed to compress folder: " << zip_url.error().message() << "\n";
                    continue;
                }

                std::string output_zip = target_name + ".zip";
                std::cout << "Downloading ZIP: " << output_zip << "...\n";
                auto res = moodle_client_.download_file(*zip_url, output_zip, session->cookie);
                if (!res) {
                    std::cerr << "Failed to download ZIP: " << res.error().message() << "\n";
                }
            } else {
                // It's a file
                std::cout << "Downloading file: " << target_name << "...\n";
                auto result = moodle_client_.download_file(it->url, target_name, session->cookie);
                if (!result) {
                    std::cerr << "Failed to download file: " << target_name << " (" << result.error().message() << ")\n";
                }
            }
        }

        return {};
    }

private:
    std::string get_folder_name(const std::string& filepath) {
        if (filepath == "/") return "/";
        std::string clean_path = filepath;
        if (clean_path.back() == '/') clean_path.pop_back();
        auto pos = clean_path.find_last_of('/');
        if (pos != std::string::npos) {
            return clean_path.substr(pos + 1);
        }
        return clean_path;
    }

    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
    std::vector<std::string> filenames_;
    bool recursive_;
};

} // namespace mstorage::commands
