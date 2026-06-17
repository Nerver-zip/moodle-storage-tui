#pragma once
#include "command.hpp"
#include "core/session_manager.hpp"
#include <iostream>

namespace mstorage::commands {

class LogoutCommand : public Command {
public:
    LogoutCommand(core::SessionManager& session_manager)
        : session_manager_(session_manager) {}

    std::expected<void, std::error_code> execute() override {
        std::cout << "Logging out and securely clearing credentials from Keyring...\n";
        
        auto res = session_manager_.clear_credentials();
        if (!res) {
            std::cerr << "Failed to fully clear credentials.\n";
            return std::unexpected(res.error());
        }

        std::cout << "Logout successful. All tokens and passwords have been removed.\n";
        return {};
    }

private:
    core::SessionManager& session_manager_;
};

} // namespace mstorage::commands
