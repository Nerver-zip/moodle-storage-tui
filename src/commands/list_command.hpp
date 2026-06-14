#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include <iostream>
#include <iomanip>

namespace mstorage::commands {

class ListCommand : public Command {
public:
    ListCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager)
        : moodle_client_(moodle_client), session_manager_(session_manager) {}

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        auto files = moodle_client_.list_files(session->cookie);
        if (!files) return std::unexpected(files.error());

        std::cout << std::left << std::setw(40) << "Filename" << std::setw(15) << "Size" << "URL" << "\n";
        std::cout << std::string(80, '-') << "\n";
        
        for (const auto& file : *files) {
            std::cout << std::left << std::setw(40) << file.filename 
                      << std::setw(15) << file.size_f 
                      << file.url << "\n";
        }
        
        return {};
    }

private:
    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
};

} // namespace mstorage::commands
