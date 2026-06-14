#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include <iostream>

namespace mstorage::commands {

class DownloadCommand : public Command {
public:
    DownloadCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager, std::string filename)
        : moodle_client_(moodle_client), session_manager_(session_manager), filename_(std::move(filename)) {}

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        auto files = moodle_client_.list_files(session->cookie);
        if (!files) return std::unexpected(files.error());

        auto it = std::find_if(files->begin(), files->end(), [&](const auto& f) {
            return f.filename == filename_;
        });

        if (it == files->end()) {
            std::cerr << "File not found: " << filename_ << "\n";
            return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        }

        std::cout << "Downloading " << filename_ << "...\n";
        return moodle_client_.download_file(it->url, filename_, session->cookie);
    }

private:
    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
    std::string filename_;
};

} // namespace mstorage::commands
