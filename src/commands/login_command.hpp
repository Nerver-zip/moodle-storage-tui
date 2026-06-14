#pragma once
#include "command.hpp"
#include "core/session_manager.hpp"
#include "moodle/moodle_client.hpp"
#include "network/http_client.hpp"
#include <iostream>

namespace mstorage::commands {

class LoginCommand : public Command {
public:
    LoginCommand(core::SessionManager& session_manager, network::HttpClient& http_client, std::string url, std::string cookie)
        : session_manager_(session_manager), http_client_(http_client), url_(std::move(url)), cookie_(std::move(cookie)) {}

    std::expected<void, std::error_code> execute() override {
        std::cout << "Validating session with Moodle...\n";

        moodle::MoodleClient client(http_client_, url_);
        auto info = client.get_draft_info(cookie_);

        if (!info) {
            std::cerr << "Login failed: Invalid cookie or Moodle unreachable.\n";
            return std::unexpected(info.error());
        }

        models::SessionData data {
            .moodle_url = url_,
            .sesskey = info->sesskey,
            .cookie = cookie_
        };

        auto result = session_manager_.save(data);
        if (result) {
            std::cout << "Login successful! Session metadata saved.\n";
            std::cout << "User Context ID: " << info->contextid << "\n";
        }
        return result;
    }

private:
    core::SessionManager& session_manager_;
    network::HttpClient& http_client_;
    std::string url_;
    std::string cookie_;
};

} // namespace mstorage::commands
