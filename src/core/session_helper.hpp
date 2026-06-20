#pragma once
#include "core/session_manager.hpp"
#include "moodle/moodle_client.hpp"
#include <expected>
#include <system_error>
#include <spdlog/spdlog.h>

namespace mstorage::core {

inline std::expected<moodle::MoodleClient::DraftInfo, std::error_code> ensure_web_session(
    moodle::MoodleClient& client,
    core::SessionManager& session_manager
) {
    auto session = session_manager.load();
    if (!session) return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));

    // Force check if web cookie works.
    if (!session->web_cookie.empty()) {
        client.set_web_cookie(session->web_cookie);
        auto draft_info = client.get_draft_info(session->web_cookie);
        if (draft_info && draft_info->sesskey != "REST_TOKEN") {
            return *draft_info; // Cookie is still valid!
        }
    }

    // Attempt to silently re-authenticate using stored credentials in system keyring
    auto creds = session_manager.load_credentials(session->moodle_url);
    if (!creds || creds->username.empty() || creds->password.empty()) {
        spdlog::warn("Web session expired/invalid and no credentials found in Keyring for silent renewal.");
        return std::unexpected(std::make_error_code(std::errc::permission_denied));
    }

    spdlog::info("Web session expired. Attempting silent re-authentication...");
    auto refreshed = client.refresh_web_session(creds->username, creds->password);
    if (!refreshed) {
        spdlog::error("Silent re-authentication failed.");
        return std::unexpected(refreshed.error());
    }

    // Save refreshed session details
    session->web_cookie = *refreshed;
    auto save_res = session_manager.save(*session);
    if (!save_res) {
        spdlog::error("Failed to save refreshed session cookie: {}", save_res.error().message());
    }

    client.set_web_cookie(*refreshed);
    spdlog::info("Web session successfully refreshed and saved.");

    // Retrieve fresh draft info with updated cookie
    auto draft_info = client.get_draft_info(*refreshed);
    if (!draft_info) {
        return std::unexpected(draft_info.error());
    }
    return *draft_info;
}

} // namespace mstorage::core
