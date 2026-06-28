#pragma once
#include "core/session_manager.hpp"
#include "moodle/moodle_client.hpp"
#include "moodle/shibboleth_auth.hpp"
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

    // 1. Validar se o token do webservice (wstoken) está ativo e funcional.
    bool token_valid = false;
    if (!session->wstoken.empty()) {
        client.set_wstoken(session->wstoken);
        auto ctx_res = client.get_user_context_id();
        if (ctx_res) {
            token_valid = true;
        } else {
            spdlog::warn("Stored wstoken is invalid or expired: {}", ctx_res.error().message());
        }
    }

    // 2. Validar se o cookie de sessão (web_cookie) está ativo e funcional.
    bool cookie_valid = false;
    moodle::MoodleClient::DraftInfo valid_draft_info;
    if (!session->web_cookie.empty()) {
        client.set_web_cookie(session->web_cookie);
        auto draft_info = client.get_draft_info(session->web_cookie);
        if (draft_info && draft_info->sesskey != "REST_TOKEN") {
            cookie_valid = true;
            valid_draft_info = *draft_info;
        } else {
            spdlog::warn("Stored web_cookie is invalid or expired.");
        }
    }

    // Se ambos estiverem válidos, a sessão está perfeitamente funcional.
    if (token_valid && cookie_valid) {
        return valid_draft_info;
    }

    // Se algum estiver inválido ou ausente, tentamos reautenticação silenciosa via chaveiro.
    auto creds = session_manager.load_credentials(session->moodle_url);
    if (!creds || creds->username.empty() || creds->password.empty()) {
        spdlog::warn("Session expired/invalid and no credentials found in Keyring for silent renewal.");
        return std::unexpected(std::make_error_code(std::errc::permission_denied));
    }

    spdlog::info("Session expired or invalid. Attempting silent re-authentication...");
    moodle::ShibbolethAuth auth(session->moodle_url);
    
    // Fazer login na web para obter um novo cookie
    auto login_res = auth.login_web(creds->username, creds->password);
    if (!login_res) {
        spdlog::error("Silent re-authentication failed during web login: {}", login_res.error().message());
        return std::unexpected(login_res.error());
    }

    std::string new_cookie = *login_res;
    std::string new_wstoken = session->wstoken;

    // Se o wstoken estiver inválido ou ausente, extraímos um novo token
    if (!token_valid || new_wstoken.empty()) {
        auto token_res = auth.extract_mobile_token(new_cookie);
        if (token_res) {
            new_wstoken = *token_res;
            spdlog::info("Successfully extracted a new webservice token during silent renewal.");
        } else {
            spdlog::warn("Failed to extract mobile token during silent renewal: {}", token_res.error().message());
        }
    }

    // Salvar os novos dados de sessão
    session->web_cookie = new_cookie;
    session->wstoken = new_wstoken;
    auto save_res = session_manager.save(*session);
    if (!save_res) {
        spdlog::error("Failed to save refreshed session: {}", save_res.error().message());
    }

    // Atualizar o client
    client.set_web_cookie(new_cookie);
    client.set_wstoken(new_wstoken);
    spdlog::info("Session successfully refreshed and saved.");

    // Retornar fresh draft info
    auto draft_info = client.get_draft_info(new_cookie);
    if (!draft_info) {
        return std::unexpected(draft_info.error());
    }
    return *draft_info;
}

} // namespace mstorage::core
