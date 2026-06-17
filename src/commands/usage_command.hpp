#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include <iostream>

namespace mstorage::commands {

class StorageUsageCommand : public Command {
public:
    StorageUsageCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager)
        : moodle_client_(moodle_client), session_manager_(session_manager) {}

    std::expected<void, std::error_code> execute() override {
        auto usage = moodle_client_.get_usage();
        if (!usage) return std::unexpected(usage.error());

        std::cout << "\nMoodle Storage Usage Report\n";
        std::cout << "===========================\n";
        std::cout << "Used:  " << format_size(usage->used_bytes) << "\n";
        std::cout << "Total: " << format_size(usage->total_bytes) << "\n";

        double percent = (static_cast<double>(usage->used_bytes) / usage->total_bytes) * 100.0;
        int bar_width = 40;
        int pos = (bar_width * percent) / 100.0;

        std::cout << "Usage: [";
        for (int i = 0; i < bar_width; ++i) {
            if (i < pos) std::cout << "#";
            else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1) << percent << "%\n";

        return {};
    }

private:
    std::string format_size(uintmax_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB"};
        int i = 0;
        double size = bytes;
        while (size > 1024 && i < 3) {
            size /= 1024;
            i++;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.2f %s", size, units[i]);
        return std::string(buf);
    }

    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
};

} // namespace mstorage::commands
