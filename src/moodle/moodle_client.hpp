#pragma once
#include "network/http_client.hpp"
#include "models/models.hpp"
#include "moodle/shibboleth_auth.hpp"
#include <nlohmann/json.hpp>
#include <regex>
#include <fstream>
#include <spdlog/spdlog.h>
#include <expected>
#include <system_error>

namespace mstorage::moodle {

class MoodleClient {
public:
    MoodleClient(network::HttpClient& client, const std::string& moodle_url)
        : client_(client), moodle_url_(moodle_url) {
        if (moodle_url_.length() > 1 && moodle_url_.back() == '/') {
            moodle_url_.pop_back();
        }
    }

    void set_wstoken(const std::string& token) {
        wstoken_ = token;
        use_wstoken_ = !token.empty();
    }

    void set_web_cookie(const std::string& cookie) {
        web_cookie_ = cookie;
    }

    struct DraftInfo {
        std::string sesskey;
        std::string itemid;
        std::string contextid;
    };

    std::expected<std::string, std::error_code> refresh_web_session(const std::string& username, const std::string& password) {
        spdlog::info("Attempting silent web re-authentication...");
        moodle::ShibbolethAuth auth(moodle_url_);
        auto login_res = auth.login_web(username, password);
        if (!login_res) return std::unexpected(login_res.error());
        web_cookie_ = *login_res;
        return web_cookie_;
    }

    std::expected<nlohmann::json, std::error_code> call_rest(const std::string& function, const cpr::Payload& params = {}) {
        std::string url = moodle_url_ + "/webservice/rest/server.php";
        cpr::Payload payload = params;
        payload.Add({"wstoken", wstoken_});
        payload.Add({"wsfunction", function});
        payload.Add({"moodlewsrestformat", "json"});

        auto response = client_.post(url, payload);
        if (!response) return std::unexpected(response.error());

        try {
            auto j = nlohmann::json::parse(*response);
            if (j.is_object() && j.contains("exception")) {
                spdlog::error("Moodle REST Exception ({}): {}", j.value("errorcode", "unknown"), j.value("message", ""));
                return std::unexpected(std::make_error_code(std::errc::permission_denied));
            }
            return j;
        } catch (...) {
            return std::unexpected(std::make_error_code(std::errc::bad_message));
        }
    }

    std::expected<int, std::error_code> get_user_context_id() {
        if (cached_context_id_ != 0) return cached_context_id_;
        auto res = call_rest("core_webservice_get_site_info");
        if (!res) return std::unexpected(res.error());
        std::string pic_url = res->value("userpictureurl", "");
        std::regex ctx_regex("/pluginfile\\.php/(\\d+)/");
        std::smatch match;
        if (std::regex_search(pic_url, match, ctx_regex)) {
            cached_context_id_ = std::stoi(match[1].str());
            return cached_context_id_;
        }
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }

    std::expected<DraftInfo, std::error_code> get_draft_info(const std::string& specific_cookie = "") {
        std::string cookie_to_use = specific_cookie.empty() ? web_cookie_ : specific_cookie;
        
        if (!cookie_to_use.empty()) {
            auto response = client_.get(moodle_url_ + "/user/files.php", cpr::Cookies{{"MoodleSession", cookie_to_use}});
            if (response) {
                if (response->find("idpv3.ufpel.edu.br") != std::string::npos || 
                    response->find("loginuserpass") != std::string::npos ||
                    response->find("Portal de autenticação UFPel") != std::string::npos) {
                    return std::unexpected(std::make_error_code(std::errc::permission_denied));
                }

                DraftInfo info;
                std::regex sesskey_regex("\"sesskey\"\\s*:\\s*\"([^\"]+)\"");
                std::smatch match;
                if (std::regex_search(*response, match, sesskey_regex)) info.sesskey = match[1];
                std::regex contextid_regex("\"contextid\"\\s*:\\s*(\\d+)");
                if (std::regex_search(*response, match, contextid_regex)) info.contextid = match[1];
                std::regex itemid_tag_regex("<input[^>]+name=\"files_filemanager\"[^>]+>");
                if (std::regex_search(*response, match, itemid_tag_regex)) {
                    std::string input_tag = match[0];
                    std::regex value_regex("value=\"(\\d+)\"");
                    if (std::regex_search(input_tag, match, value_regex)) info.itemid = match[1];
                }
                if (!info.sesskey.empty() && !info.itemid.empty()) return info;
            }
        }

        if (use_wstoken_) {
            auto res = call_rest("core_user_prepare_private_files_for_edition");
            if (!res) return std::unexpected(res.error());
            DraftInfo info;
            info.itemid = std::to_string((*res)["draftitemid"].get<int>());
            info.sesskey = "REST_TOKEN";
            info.contextid = std::to_string(get_user_context_id().value_or(0));
            return info;
        }

        return std::unexpected(std::make_error_code(std::errc::permission_denied));
    }

    std::expected<void, std::error_code> upload_file(const std::string& file_path, const std::string& remote_path, const DraftInfo& info, const std::string& specific_cookie = "") {
        if (info.sesskey == "REST_TOKEN") {
            std::string url = moodle_url_ + "/webservice/upload.php?token=" + wstoken_ + "&itemid=" + info.itemid + "&filepath=" + remote_path;
            cpr::Multipart multipart{{"file", cpr::File{file_path}}};
            auto response = client_.post_multipart(url, multipart);
            return response ? std::expected<void, std::error_code>{} : std::unexpected(response.error());
        }
        std::string cookie_to_use = specific_cookie.empty() ? web_cookie_ : specific_cookie;
        cpr::Multipart multipart{
            {"repo_id", "4"}, {"sesskey", info.sesskey}, {"client_id", "mstorage_" + info.itemid},
            {"itemid", info.itemid}, {"ctx_id", info.contextid}, {"savepath", remote_path},
            {"repo_upload_file", cpr::File{file_path}}, {"title", std::filesystem::path(file_path).filename().string()}
        };
        auto response = client_.post_multipart(moodle_url_ + "/repository/repository_ajax.php?action=upload", multipart, cpr::Cookies{{"MoodleSession", cookie_to_use}});
        return response ? std::expected<void, std::error_code>{} : std::unexpected(response.error());
    }

    std::expected<void, std::error_code> commit_draft(const DraftInfo& info, const std::string& specific_cookie = "") {
        std::string cookie_to_use = specific_cookie.empty() ? web_cookie_ : specific_cookie;
        if (info.sesskey == "REST_TOKEN") {
            auto res = call_rest("core_user_update_private_files", {{"draftitemid", info.itemid}});
            return res ? std::expected<void, std::error_code>{} : std::unexpected(res.error());
        }
        nlohmann::json j = nlohmann::json::array();
        std::string formdata = "sesskey=" + info.sesskey + "&_qf__core_user_form_private_files=1&files_filemanager=" + info.itemid;
        j.push_back({{"index", 0}, {"methodname", "core_form_dynamic_form"}, {"args", {{"formdata", formdata}, {"form", "core_user\\form\\private_files"}}}});
        auto response = client_.post_raw(moodle_url_ + "/lib/ajax/service.php?sesskey=" + info.sesskey, j.dump(), "application/json", cpr::Cookies{{"MoodleSession", cookie_to_use}});
        return response ? std::expected<void, std::error_code>{} : std::unexpected(response.error());
    }

    std::expected<std::vector<models::MoodleFile>, std::error_code> list_files([[maybe_unused]] const std::string& unused_cookie = "", const std::string& filepath = "/") {
        auto ctx_id = get_user_context_id();
        if (!ctx_id) return std::unexpected(ctx_id.error());
        auto res = call_rest("core_files_get_files", {
            {"contextid", std::to_string(*ctx_id)}, {"component", "user"}, {"filearea", "private"},
            {"itemid", "0"}, {"filepath", filepath}, {"filename", ""}
        });
        if (!res) return std::unexpected(res.error());
        std::vector<models::MoodleFile> files;
        if (res->contains("files") && (*res)["files"].is_array()) {
            for (auto& item : (*res)["files"]) {
                models::MoodleFile f;
                bool is_dir = item.value("isdir", false);
                f.filename = is_dir ? "." : item.value("filename", "");
                f.filepath = item.value("filepath", "");
                if (item.contains("url") && item["url"].is_string()) {
                    f.url = item["url"].get<std::string>();
                    if (!f.url.empty()) f.url += (f.url.find('?') == std::string::npos ? "?" : "&") + std::string("token=") + wstoken_;
                }
                f.size = item.value("filesize", 0);
                f.size_f = format_size(f.size);
                f.datemodified = item.value("timemodified", 0);
                files.push_back(std::move(f));
            }
        }
        return files;
    }

    std::expected<models::StorageUsage, std::error_code> get_usage([[maybe_unused]] const std::string& unused_cookie = "") {
        auto res = call_rest("core_user_get_private_files_info");
        if (!res) return std::unexpected(res.error());
        return models::StorageUsage{res->value("filesize", uintmax_t(0)), 100 * 1024 * 1024};
    }

    std::expected<void, std::error_code> download_file(const std::string& url, const std::string& output_path, const std::string& specific_cookie = "") {
        std::string cookie_to_use = specific_cookie.empty() ? web_cookie_ : specific_cookie;
        auto response = client_.get(url, cookie_to_use.empty() ? cpr::Cookies{} : cpr::Cookies{{"MoodleSession", cookie_to_use}});
        if (!response) return std::unexpected(response.error());

        if (response->find("idpv3.ufpel.edu.br") != std::string::npos || 
            response->find("loginuserpass") != std::string::npos ||
            response->find("Portal de autenticação UFPel") != std::string::npos) {
            return std::unexpected(std::make_error_code(std::errc::permission_denied));
        }

        std::ofstream file(output_path, std::ios::binary);
        if (!file.is_open()) return std::unexpected(std::make_error_code(std::errc::io_error));
        file.write(response->data(), response->size());
        return {};
    }

    std::expected<void, std::error_code> create_folder(const std::string& folder_name, const std::string& parent_path, const DraftInfo& info, const std::string& specific_cookie = "") {
        if (info.sesskey == "REST_TOKEN") return {};
        std::string cookie_to_use = specific_cookie.empty() ? web_cookie_ : specific_cookie;
        cpr::Payload payload{{"sesskey", info.sesskey}, {"itemid", info.itemid}, {"filepath", parent_path}, {"newdirname", folder_name}};
        auto response = client_.post(moodle_url_ + "/repository/draftfiles_ajax.php?action=mkdir", payload, cpr::Cookies{{"MoodleSession", cookie_to_use}});
        return response ? std::expected<void, std::error_code>{} : std::unexpected(response.error());
    }

    std::expected<std::string, std::error_code> zip_folder(const std::string& folder_path, const DraftInfo& info, const std::string& specific_cookie = "") {
        std::string cookie_to_use = specific_cookie.empty() ? web_cookie_ : specific_cookie;
        if (cookie_to_use.empty()) return std::unexpected(std::make_error_code(std::errc::not_supported));
        nlohmann::json selected = {{{"filepath", folder_path}, {"filename", "."}}};
        cpr::Payload payload{{"sesskey", info.sesskey}, {"filepath", "/"}, {"itemid", info.itemid}, {"selected", selected.dump()}};
        auto response = client_.post(moodle_url_ + "/repository/draftfiles_ajax.php?action=downloadselected", payload, cpr::Cookies{{"MoodleSession", cookie_to_use}});
        if (!response) return std::unexpected(response.error());
        return nlohmann::json::parse(*response).value("fileurl", "");
    }

    std::expected<void, std::error_code> delete_items(const std::vector<models::DeleteItem>& items, const DraftInfo& info, const std::string& specific_cookie = "") {
        std::string cookie_to_use = specific_cookie.empty() ? web_cookie_ : specific_cookie;
        if (!cookie_to_use.empty()) {
            nlohmann::json selected = nlohmann::json::array();
            for (const auto& item : items) selected.push_back({{"filepath", item.is_folder ? item.parent_path + item.name + "/" : item.parent_path}, {"filename", item.is_folder ? "." : item.name}});
            cpr::Payload payload{{"sesskey", info.sesskey}, {"itemid", info.itemid}, {"selected", selected.dump()}};
            auto response = client_.post(moodle_url_ + "/repository/draftfiles_ajax.php?action=deleteselected", payload, cpr::Cookies{{"MoodleSession", cookie_to_use}});
            if (response) return {};
        }

        if (use_wstoken_) {
            std::vector<std::pair<std::string, std::string>> all_files;
            for (const auto& item : items) {
                if (item.is_folder) collect_files_recursive(info.itemid, item.parent_path + item.name + "/", all_files);
                all_files.push_back({item.parent_path, item.name});
            }
            if (all_files.empty()) return {};
            cpr::Payload payload{{"draftitemid", info.itemid}};
            for (size_t i = 0; i < all_files.size(); ++i) {
                std::string base = "files[" + std::to_string(i) + "]";
                payload.Add({base + "[filepath]", all_files[i].first});
                payload.Add({base + "[filename]", all_files[i].second});
            }
            auto res = call_rest("core_files_delete_draft_files", payload);
            return res ? std::expected<void, std::error_code>{} : std::unexpected(res.error());
        }
        return std::unexpected(std::make_error_code(std::errc::permission_denied));
    }

private:
    void collect_files_recursive(const std::string& draftid, const std::string& path, std::vector<std::pair<std::string, std::string>>& out) {
        auto res = call_rest("core_files_get_files", {
            {"contextid", "0"}, {"component", "user"}, {"filearea", "draft"},
            {"itemid", draftid}, {"filepath", path}, {"filename", ""}
        });
        if (!res || !res->contains("files")) return;
        for (const auto& f : (*res)["files"]) {
            if (f.value("isdir", false)) {
                collect_files_recursive(draftid, f.value("filepath", ""), out);
                out.push_back({path, f.value("filename", "")});
            } else {
                out.push_back({f.value("filepath", ""), f.value("filename", "")});
            }
        }
    }

    std::string format_size(uintmax_t bytes) {
        if (bytes == 0) return "-";
        const char* units[] = {"B", "KB", "MB", "GB"};
        int i = 0;
        double size = static_cast<double>(bytes);
        while (size > 1024 && i < 3) { size /= 1024; i++; }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.2f %s", size, units[i]);
        return std::string(buf);
    }

    network::HttpClient& client_;
    std::string moodle_url_;
    std::string wstoken_;
    std::string web_cookie_;
    bool use_wstoken_ = false;
    int cached_context_id_ = 0;
};

} // namespace mstorage::moodle
