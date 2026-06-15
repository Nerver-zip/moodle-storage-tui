#pragma once
#include <string>
#include <expected>
#include <system_error>
#include <cpr/cpr.h>
#include <spdlog/spdlog.h>
#include <regex>
#include <iostream>
#include <sstream>
#include <vector>

namespace mstorage::moodle {

class ShibbolethAuth {
public:
    ShibbolethAuth(const std::string& moodle_url) : moodle_url_(moodle_url) {
        if (moodle_url_.length() > 1 && moodle_url_.back() == '/') {
            moodle_url_.pop_back();
        }
    }

    struct AuthResult {
        std::string permanent_token;
    };

    std::expected<std::string, std::error_code> login_web(const std::string& username, const std::string& password) {
        cpr::Session session;
        session.SetVerifySsl(true);

        std::string login_url = moodle_url_ + "/auth/shibboleth/index.php";
        spdlog::debug("Starting Shibboleth flow at {}", login_url);

        session.SetUrl(cpr::Url{login_url});
        auto r1 = session.Get();
        if (r1.error) {
            spdlog::error("Initial GET failed: {}", r1.error.message);
            return std::unexpected(std::make_error_code(std::errc::network_unreachable));
        }

        std::string html = r1.text;
        std::regex action_regex("<form[^>]*action=\"([^\"]+)\"", std::regex_constants::icase);
        std::smatch match;
        if (!std::regex_search(html, match, action_regex)) {
            spdlog::error("Could not find login form action in IdP page.");
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        
        std::string form_action = match[1].str();
        if (form_action.length() > 0 && form_action[0] == '/') {
            std::string current_url = r1.url.str();
            size_t slash_pos = current_url.find('/', 8);
            std::string base = current_url.substr(0, slash_pos);
            form_action = base + form_action;
        }

        cpr::Payload payload{
            {"username", username},
            {"password", password}
        };

        std::regex input_regex("<input[^>]*type=\"hidden\"[^>]*name=\"([^\"]+)\"[^>]*value=\"([^\"]*)\"[^>]*>", std::regex_constants::icase);
        auto words_begin = std::sregex_iterator(html.begin(), html.end(), input_regex);
        auto words_end = std::sregex_iterator();
        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch m = *i;
            payload.Add({m[1].str(), m[2].str()});
        }

        spdlog::debug("Posting credentials to {}", form_action);
        session.SetUrl(cpr::Url{form_action});
        session.SetPayload(payload);
        auto r2 = session.Post();

        if (r2.error) {
            spdlog::error("IdP POST failed: {}", r2.error.message);
            return std::unexpected(std::make_error_code(std::errc::network_unreachable));
        }

        html = r2.text;
        if (html.find("SAMLResponse") == std::string::npos) {
            std::ofstream out("/tmp/mstorage_idp_error.html");
            out << html;
            spdlog::error("Login failed or no SAMLResponse found. Check credentials. (HTML dumped to /tmp/mstorage_idp_error.html)");
            return std::unexpected(std::make_error_code(std::errc::permission_denied));
        }

        std::regex saml_action_regex("<form[^>]*action=\"([^\"]+)\"", std::regex_constants::icase);
        if (!std::regex_search(html, match, saml_action_regex)) {
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        std::string saml_action = match[1].str();

        std::regex saml_val_regex("name=\"SAMLResponse\"[^>]*value=\"([^\"]+)\"", std::regex_constants::icase);
        if (!std::regex_search(html, match, saml_val_regex)) {
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        std::string saml_response = match[1].str();

        std::regex relay_val_regex("name=\"RelayState\"[^>]*value=\"([^\"]+)\"", std::regex_constants::icase);
        std::string relay_state = "";
        if (std::regex_search(html, match, relay_val_regex)) {
            relay_state = match[1].str();
        }

        spdlog::debug("Posting SAML to Moodle at {}", saml_action);
        session.SetUrl(cpr::Url{saml_action});
        
        cpr::Payload saml_payload{
            {"SAMLResponse", saml_response},
            {"RelayState", relay_state}
        };
        
        session.SetPayload(saml_payload);
        auto r3 = session.Post();

        if (r3.error) {
            return std::unexpected(std::make_error_code(std::errc::network_unreachable));
        }

        std::string moodle_session_cookie = "";
        for (const auto& cookie : r3.cookies) {
            if (cookie.GetName() == "MoodleSession") {
                moodle_session_cookie = cookie.GetValue();
                break;
            }
        }

        if (moodle_session_cookie.empty()) {
            spdlog::error("Failed to obtain MoodleSession cookie after SAML post.");
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }

        return moodle_session_cookie;
    }

    std::expected<std::string, std::error_code> extract_mobile_token(const std::string& moodle_session_cookie) {
        std::string launch_url = moodle_url_ + "/admin/tool/mobile/launch.php?service=moodle_mobile_app&passport=12345&urlscheme=mstorageapp";
        
        auto r4 = cpr::Get(
            cpr::Url{launch_url}, 
            cpr::Cookies{{"MoodleSession", moodle_session_cookie}},
            cpr::Redirect{0L}
        );
        
        std::string location = "";
        if (r4.header.count("Location")) location = r4.header.at("Location");
        else if (r4.header.count("location")) location = r4.header.at("location");

        if (location.empty() || (!location.starts_with("mstorageapp://") && !location.starts_with("moodlemobile://"))) {
            spdlog::error("Failed to intercept token redirect. Got status {} and location: {}", r4.status_code, location);
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }

        size_t token_pos = location.find("token=");
        if (token_pos == std::string::npos) {
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }

        size_t end_pos = location.find("&", token_pos);
        if (end_pos == std::string::npos) end_pos = location.length();
        
        std::string encoded_token = location.substr(token_pos + 6, end_pos - (token_pos + 6));
        
        std::string clean_encoded;
        for (size_t i = 0; i < encoded_token.length(); ++i) {
            if (encoded_token[i] == '%' && i + 2 < encoded_token.length()) {
                int value;
                std::stringstream ss;
                ss << std::hex << encoded_token.substr(i + 1, 2);
                ss >> value;
                clean_encoded += (char)value;
                i += 2;
            } else {
                clean_encoded += encoded_token[i];
            }
        }

        int missing_padding = clean_encoded.length() % 4;
        if (missing_padding > 0) {
            clean_encoded.append(4 - missing_padding, '=');
        }

        std::string decoded;
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;
        int val = 0, valb = -8;
        for (unsigned char c : clean_encoded) {
            if (T[c] == -1) break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                decoded.push_back(char((val >> valb) & 0xFF));
                valb -= 8;
            }
        }

        std::string final_token = decoded;
        size_t sep = decoded.find(":::");
        if (sep != std::string::npos) {
            // "siteid:::token" or "siteid:::token:::privatetoken"
            size_t end_sep = decoded.find(":::", sep + 3);
            if (end_sep != std::string::npos) {
                final_token = decoded.substr(sep + 3, end_sep - (sep + 3));
            } else {
                final_token = decoded.substr(sep + 3);
            }
        }

        return final_token;
    }

private:
    std::string moodle_url_;
};

} // namespace mstorage::moodle
