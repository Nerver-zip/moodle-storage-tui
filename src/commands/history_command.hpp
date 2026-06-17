#pragma once
#include "command.hpp"
#include "storage/upload_history.hpp"
#include <iostream>
#include <iomanip>

namespace mstorage::commands {

class HistoryCommand : public Command {
public:
    HistoryCommand(storage::HistoryManager& history_manager, bool clear_flag = false)
        : history_manager_(history_manager), clear_flag_(clear_flag) {}

    std::expected<void, std::error_code> execute() override {
        if (clear_flag_) {
            auto res = history_manager_.clear();
            if (!res) {
                std::cerr << "Failed to clear history.\n";
                return std::unexpected(res.error());
            }
            std::cout << "Upload history cleared successfully.\n";
            return {};
        }

        auto history = history_manager_.get_history(20);
        if (!history) return std::unexpected(history.error());

        if (history->empty()) {
            std::cout << "No upload history found.\n";
            return {};
        }

        std::cout << std::left << std::setw(30) << "Filename" << std::setw(30) << "Type" << "Timestamp" << "\n";
        std::cout << std::string(80, '-') << "\n";
        
        for (const auto& entry : *history) {
            std::string display_name = entry.filename;
            if (display_name.length() > 27) {
                display_name = display_name.substr(0, 24) + "...";
            }
            std::cout << std::left << std::setw(30) << display_name 
                      << std::setw(30) << entry.url 
                      << entry.timestamp << "\n";
        }
        
        return {};
    }

private:
    storage::HistoryManager& history_manager_;
    bool clear_flag_;
};

} // namespace mstorage::commands
