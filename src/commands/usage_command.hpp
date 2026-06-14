#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include <iostream>
#include <iomanip>
#include <format>

namespace mstorage::commands {

class StorageUsageCommand : public Command {
public:
    StorageUsageCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager)
        : moodle_client_(moodle_client), session_manager_(session_manager) {}

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        auto usage = moodle_client_.get_usage(session->cookie);
        if (!usage) return std::unexpected(usage.error());

        double used_mb = static_cast<double>(usage->used_bytes) / (1024.0 * 1024.0);
        double total_mb = static_cast<double>(usage->total_bytes) / (1024.0 * 1024.0);
        double percent = (used_mb / total_mb) * 100.0;

        std::cout << "\nMoodle Storage Usage Report\n";
        std::cout << "===========================\n";
        std::cout << std::format("Used:  {:.2f} MB\n", used_mb);
        std::cout << std::format("Total: {:.2f} MB\n", total_mb);
        
        int bar_width = 40;
        int pos = static_cast<int>(bar_width * (percent / 100.0));
        
        std::cout << "Usage: [";
        for (int i = 0; i < bar_width; ++i) {
            if (i < pos) std::cout << "#";
            else std::cout << " ";
        }
        std::cout << std::format("] {:.1f}%\n", percent);

        if (percent > 90.0) {
            std::cout << "\nWARNING: You have used over 90% of your storage limit!\n";
        }

        return {};
    }

private:
    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
};

} // namespace mstorage::commands
