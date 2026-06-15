#pragma once
#include "command.hpp"
#include "core/session_manager.hpp"
#include "moodle/shibboleth_auth.hpp"
#include "network/http_client.hpp"
#include <iostream>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace mstorage::commands {

class LoginCommand : public Command {
public:
    LoginCommand(core::SessionManager& session_manager, network::HttpClient& http_client, std::string url)
        : session_manager_(session_manager), http_client_(http_client), url_(std::move(url)) {}

    std::expected<void, std::error_code> execute() override {
        if (url_.length() > 1 && url_.back() == '/') {
            url_.pop_back();
        }

        std::cout << "==========================================================\n";
        std::cout << "              Moodle Storage Authentication\n";
        std::cout << "==========================================================\n\n";

        std::string username, password;
        std::cout << "Moodle Username (CPF): ";
        std::getline(std::cin, username);

        std::cout << "Password: ";
        password = get_password();
        std::cout << "\n\nAuthenticating with Moodle (Shibboleth SSO)...\n";

        moodle::ShibbolethAuth auth(url_);
        auto login_res = auth.login_web(username, password);
        
        if (!login_res) {
            std::cerr << "Login failed. Please check your credentials.\n";
            return std::unexpected(login_res.error());
        }

        std::string session_cookie = *login_res;
        std::cout << "✅ Web Session successfully acquired!\n\n";

        std::string response;
        std::cout << "Do you want to extract the Permanent Mobile Token? (Y/n): ";
        std::getline(std::cin, response);

        if (response.empty() || response == "Y" || response == "y") {
            std::cout << "Extracting permanent token...\n";
            auto token_res = auth.extract_mobile_token(session_cookie);
            if (!token_res) {
                std::cerr << "❌ Failed to extract mobile token.\n";
                return std::unexpected(token_res.error());
            }

            std::cout << "✅ Permanent token captured successfully!\n";
            
            models::SessionData data {
                .moodle_url = url_,
                .sesskey = "TOKEN_AUTH",
                .cookie = *token_res
            };

            auto res = session_manager_.save(data);
            if (!res) {
                std::cerr << "Failed to save session securely.\n";
                return std::unexpected(res.error());
            }
            std::cout << "Secure authentication complete. You can now use all commands.\n";
        } else {
            // Save temporary web session
            models::SessionData data {
                .moodle_url = url_,
                .sesskey = "TEMPORARY",
                .cookie = session_cookie
            };
            auto res = session_manager_.save(data);
            if (!res) return std::unexpected(res.error());
            std::cout << "Temporary web session saved.\n";
        }

        return {};
    }

private:
    std::string get_password() {
        std::string password;
        struct termios oldt, newt;
        
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);

        std::getline(std::cin, password);

        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return password;
    }

    core::SessionManager& session_manager_;
    network::HttpClient& http_client_;
    std::string url_;
};

} // namespace mstorage::commands
