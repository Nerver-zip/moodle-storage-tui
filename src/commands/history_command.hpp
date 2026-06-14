#pragma once
#include "command.hpp"
#include "storage/upload_history.hpp"
#include <iostream>
#include <iomanip>

namespace mstorage::commands {

class HistoryCommand : public Command {
public:
    HistoryCommand(storage::HistoryManager& history_manager)
        : history_manager_(history_manager) {}

    std::expected<void, std::error_code> execute() override {
        auto history = history_manager_.get_history(20);
        if (!history) return std::unexpected(history.error());

        if (history->empty()) {
            std::cout << "No upload history found.\n";
            return {};
        }

        std::cout << std::left << std::setw(30) << "Filename" << std::setw(30) << "Moodle URL" << "Timestamp" << "\n";
        std::cout << std::string(80, '-') << "\n";
        
        for (const auto& entry : *history) {
            std::cout << std::left << std::setw(30) << entry.filename 
                      << std::setw(30) << entry.url 
                      << entry.timestamp << "\n";
        }
        
        return {};
    }

private:
    storage::HistoryManager& history_manager_;
};

} // namespace mstorage::commands
