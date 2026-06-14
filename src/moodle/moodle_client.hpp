#pragma once
#include "network/http_client.hpp"
#include "models/models.hpp"
#include <nlohmann/json.hpp>
#include <regex>
#include <fstream>
#include <spdlog/spdlog.h>

namespace mstorage::moodle {

class MoodleClient {
public:
    MoodleClient(network::HttpClient& client, const std::string& moodle_url)
        : client_(client), moodle_url_(moodle_url) {}

    struct DraftInfo {
        std::string sesskey;
        std::string itemid;
        std::string contextid;
    };

    std::expected<DraftInfo, std::error_code> get_draft_info(const std::string& cookie) {
        auto response = client_.get(moodle_url_ + "/user/files.php", cpr::Cookies{{"MoodleSession", cookie}});
        if (!response) return std::unexpected(response.error());

        DraftInfo info;
        
        // Extract sesskey
        std::regex sesskey_regex("\"sesskey\"\\s*:\\s*\"([^\"]+)\"");
        std::smatch match;
        if (std::regex_search(*response, match, sesskey_regex)) {
            info.sesskey = match[1];
        }

        // Extract contextid
        std::regex contextid_regex("\"contextid\"\\s*:\\s*(\\d+)");
        if (std::regex_search(*response, match, contextid_regex)) {
            info.contextid = match[1];
        }

        // Extract itemid
        std::regex itemid_tag_regex("<input[^>]+name=\"files_filemanager\"[^>]+>");
        if (std::regex_search(*response, match, itemid_tag_regex)) {
            std::string input_tag = match[0];
            std::regex value_regex("value=\"(\\d+)\"");
            if (std::regex_search(input_tag, match, value_regex)) {
                info.itemid = match[1];
            }
        }

        if (info.sesskey.empty()) {
            spdlog::error("Failed to extract sesskey from Moodle response.");
            return std::unexpected(std::make_error_code(std::errc::permission_denied));
        }
        if (info.itemid.empty()) {
            spdlog::error("Failed to extract itemid (draft area) from Moodle response.");
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }
        if (info.contextid.empty()) {
            spdlog::warn("Failed to extract contextid, using default (0).");
            info.contextid = "0";
        }

        spdlog::debug("Extracted sesskey: {}, itemid: {}, contextid: {}", info.sesskey, info.itemid, info.contextid);
        return info;
    }

    std::expected<void, std::error_code> upload_file(const std::string& file_path, const DraftInfo& info, const std::string& cookie) {
        std::string client_id = "mstorage_" + info.itemid; // Simple deterministic client_id
        
        cpr::Multipart multipart{
            {"repo_id", "4"},
            {"p", ""},
            {"page", ""},
            {"env", "filemanager"},
            {"sesskey", info.sesskey},
            {"client_id", client_id},
            {"itemid", info.itemid},
            {"maxbytes", "104857600"},
            {"areamaxbytes", "104857600"},
            {"ctx_id", info.contextid},
            {"savepath", "/"},
            {"repo_upload_file", cpr::File{file_path}},
            {"title", std::filesystem::path(file_path).filename().string()},
            {"author", "Moodle Storage TUI"},
            {"license", "allrightsreserved"}
        };

        auto response = client_.post_multipart(moodle_url_ + "/repository/repository_ajax.php?action=upload", multipart, cpr::Cookies{{"MoodleSession", cookie}});
        if (!response) return std::unexpected(response.error());

        spdlog::debug("Upload response: {}", *response);
        return {};
    }

    std::expected<void, std::error_code> commit_draft(const DraftInfo& info, const std::string& cookie) {
        // Moodle uses a dynamic form AJAX request to save private files
        nlohmann::json j = nlohmann::json::array();
        std::string formdata = "sesskey=" + info.sesskey + "&_qf__core_user_form_private_files=1&files_filemanager=" + info.itemid;
        
        j.push_back({
            {"index", 0},
            {"methodname", "core_form_dynamic_form"},
            {"args", {
                {"formdata", formdata},
                {"form", "core_user\\form\\private_files"}
            }}
        });

        std::string url = moodle_url_ + "/lib/ajax/service.php?sesskey=" + info.sesskey + "&info=core_form_dynamic_form";
        
        auto response = client_.post_raw(url, j.dump(), "application/json", cpr::Cookies{{"MoodleSession", cookie}});

        if (!response) {
            return std::unexpected(response.error());
        }

        spdlog::debug("Commit response: {}", *response);
        return {};
    }

    std::expected<std::vector<models::MoodleFile>, std::error_code> list_files(const std::string& cookie) {
        // First get draft info to ensure we have a sesskey and itemid
        auto info = get_draft_info(cookie);
        if (!info) return std::unexpected(info.error());

        std::string client_id = "mstorage_" + info->itemid;
        cpr::Payload payload{
            {"sesskey", info->sesskey},
            {"client_id", client_id},
            {"filepath", "/"},
            {"itemid", info->itemid}
        };

        auto response = client_.post(moodle_url_ + "/repository/draftfiles_ajax.php?action=list", payload, cpr::Cookies{{"MoodleSession", cookie}});
        if (!response) return std::unexpected(response.error());

        try {
            auto j = nlohmann::json::parse(*response);
            std::vector<models::MoodleFile> files;
            if (j.contains("list")) {
                for (auto& item : j["list"]) {
                    models::MoodleFile f;
                    f.filename = item["filename"];
                    f.filepath = item["filepath"];
                    f.url = item["url"];
                    f.size = item["size"];
                    f.size_f = item["filesize"];
                    f.datemodified = item["datemodified"];
                    files.push_back(std::move(f));
                }
            }
            return files;
        } catch (const std::exception& e) {
            spdlog::error("Failed to parse file list: {}", e.what());
            return std::unexpected(std::make_error_code(std::errc::bad_message));
        }
    }

    std::expected<models::StorageUsage, std::error_code> get_usage(const std::string& cookie) {
        auto info = get_draft_info(cookie);
        if (!info) return std::unexpected(info.error());

        std::string client_id = "mstorage_" + info->itemid;
        cpr::Payload payload{
            {"sesskey", info->sesskey},
            {"client_id", client_id},
            {"filepath", "/"},
            {"itemid", info->itemid}
        };

        auto response = client_.post(moodle_url_ + "/repository/draftfiles_ajax.php?action=list", payload, cpr::Cookies{{"MoodleSession", cookie}});
        if (!response) return std::unexpected(response.error());

        try {
            auto j = nlohmann::json::parse(*response);
            models::StorageUsage usage;
            usage.used_bytes = j.value("filesize", 0);
            usage.total_bytes = 100 * 1024 * 1024; // Default 100MB
            return usage;
        } catch (const std::exception& e) {
            spdlog::error("Failed to parse usage info: {}", e.what());
            return std::unexpected(std::make_error_code(std::errc::bad_message));
        }
    }

    std::expected<void, std::error_code> download_file(const std::string& url, const std::string& output_path, const std::string& cookie) {
        spdlog::debug("Downloading from URL: {}", url);
        auto response = client_.get(url, cpr::Cookies{{"MoodleSession", cookie}});
        
        if (!response) {
            spdlog::error("Download request failed: {}", response.error().message());
            return std::unexpected(response.error());
        }

        std::ofstream file(output_path, std::ios::binary);
        if (!file.is_open()) {
            spdlog::error("Failed to open output file for writing: {}", output_path);
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }
        
        file.write(response->data(), response->size());
        spdlog::debug("Download completed: {} bytes", response->size());
        return {};
    }

    std::expected<void, std::error_code> delete_file(const std::string& filename, const DraftInfo& info, const std::string& cookie) {
        std::string client_id = "mstorage_" + info.itemid;
        
        nlohmann::json selected = nlohmann::json::array();
        selected.push_back({
            {"filepath", "/"},
            {"filename", filename}
        });

        cpr::Payload payload{
            {"sesskey", info.sesskey},
            {"client_id", client_id},
            {"filepath", "/"},
            {"itemid", info.itemid},
            {"selected", selected.dump()}
        };

        auto response = client_.post(moodle_url_ + "/repository/draftfiles_ajax.php?action=deleteselected", payload, cpr::Cookies{{"MoodleSession", cookie}});
        if (!response) return std::unexpected(response.error());

        spdlog::debug("Delete response: {}", *response);
        return {};
    }

private:
    network::HttpClient& client_;
    std::string moodle_url_;
};

} // namespace mstorage::moodle
