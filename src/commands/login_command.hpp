#pragma once
#include "command.hpp"
#include "core/session_manager.hpp"
#include <iostream>

namespace mstorage::commands {

class LoginCommand : public Command {
public:
    LoginCommand(core::SessionManager& session_manager, std::string url, std::string cookie)
        : session_manager_(session_manager), url_(std::move(url)), cookie_(std::move(cookie)) {}

    std::expected<void, std::error_code> execute() override {
        models::SessionData data {
            .moodle_url = url_,
            .sesskey = "",
            .cookie = cookie_
        };

        auto result = session_manager_.save(data);
        if (result) {
            std::cout << "Login data saved successfully.\n";
        }
        return result;
    }

private:
    core::SessionManager& session_manager_;
    std::string url_;
    std::string cookie_;
};

} // namespace mstorage::commands
