#include "updater.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace mstorage::core {

Version parse_version(std::string_view version_str) {
    std::string s(version_str);
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) {
        s.erase(0, 1);
    }
    
    std::vector<int> parts;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, '.')) {
        try {
            parts.push_back(std::stoi(token));
        } catch (...) {
            parts.push_back(0);
        }
    }
    
    Version v;
    if (parts.size() > 0) v.major = parts[0];
    if (parts.size() > 1) v.minor = parts[1];
    if (parts.size() > 2) v.patch = parts[2];
    return v;
}

std::expected<void, std::error_code> Updater::perform_update(network::HttpClient& http_client, std::string_view current_version) {
    // 1. Obter caminho do executável atual
    std::error_code ec;
    auto current_exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        // Fallback para caminhos comuns se read_symlink falhar
        current_exe = std::filesystem::absolute("./mstorage", ec);
        if (ec) {
            std::cerr << "Error: Could not determine current executable path.\n";
            return std::unexpected(make_error_code(std::errc::no_such_file_or_directory));
        }
    }

    auto exe_dir = current_exe.parent_path();

    // 2. Verificar se o diretório do executável é gravável
    auto test_file = exe_dir / ".mstorage_update_test";
    {
        std::ofstream test_stream(test_file);
        if (!test_stream) {
            std::cerr << "\nError: Permission denied. You do not have write permissions to " << exe_dir.string() << ".\n";
            std::cerr << "Please run with sudo to update the binary:\n";
            std::cerr << "  sudo mstorage --update\n\n";
            return std::unexpected(std::make_error_code(std::errc::permission_denied));
        }
    }
    std::filesystem::remove(test_file, ec);

    // 3. Consultar API do GitHub
    std::string api_url = "https://api.github.com/repos/Nerver-zip/moodle-storage-tui/releases/latest";
    std::cout << "Checking for updates from GitHub...\n";
    auto api_res = http_client.get(api_url);
    if (!api_res) {
        std::cerr << "Error: Could not check for updates from GitHub. Network is unreachable.\n";
        return std::unexpected(api_res.error());
    }

    nlohmann::json release_json;
    try {
        release_json = nlohmann::json::parse(*api_res);
    } catch (const std::exception& e) {
        std::cerr << "Error: Failed to parse GitHub API response.\n";
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    }

    if (!release_json.contains("tag_name")) {
        std::cerr << "Error: Invalid release metadata from GitHub.\n";
        return std::unexpected(std::make_error_code(std::errc::bad_message));
    }

    std::string tag_name = release_json["tag_name"];
    std::string body = release_json.contains("body") ? release_json["body"].get<std::string>() : "";

    auto local_v = parse_version(current_version);
    auto remote_v = parse_version(tag_name);

    if (remote_v <= local_v) {
        std::cout << "Moodle Storage is already up-to-date (version " << current_version << ").\n";
        return {};
    }

    std::cout << "A new version is available: " << tag_name << " (current: " << current_version << ")\n";

    // 4. Localizar o asset correspondente
    std::string download_url = "";
    std::string target_asset_name = "moodle-storage-linux-amd64.tar.gz";
    if (release_json.contains("assets") && release_json["assets"].is_array()) {
        for (const auto& asset : release_json["assets"]) {
            if (asset.contains("name") && asset["name"] == target_asset_name) {
                download_url = asset["browser_download_url"];
                break;
            }
        }
    }

    if (download_url.empty()) {
        std::cerr << "Error: Could not find target asset " << target_asset_name << " in release " << tag_name << ".\n";
        return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    }

    // 5. Baixar o asset
    std::cout << "Downloading update from " << download_url << "...\n";
    auto download_res = http_client.get(download_url);
    if (!download_res) {
        std::cerr << "Error: Failed to download the update package.\n";
        return std::unexpected(download_res.error());
    }

    auto temp_tar = std::filesystem::temp_directory_path() / "mstorage-update.tar.gz";
    {
        std::ofstream tar_stream(temp_tar, std::ios::binary);
        if (!tar_stream) {
            std::cerr << "Error: Failed to open temporary file for writing download.\n";
            return std::unexpected(std::make_error_code(std::errc::io_error));
        }
        tar_stream.write(download_res->data(), download_res->size());
    }

    // 6. Extrair o binário
    std::cout << "Extracting archive...\n";
    auto temp_dir = std::filesystem::temp_directory_path();
    std::string tar_cmd = "tar -xzf " + temp_tar.string() + " -C " + temp_dir.string();
    int tar_res = std::system(tar_cmd.c_str());
    if (tar_res != 0) {
        std::cerr << "Error: Extraction failed with exit code " << tar_res << ".\n";
        std::filesystem::remove(temp_tar, ec);
        return std::unexpected(std::make_error_code(std::errc::executable_format_error));
    }

    std::filesystem::remove(temp_tar, ec);

    auto extracted_exe = temp_dir / "mstorage";
    if (!std::filesystem::exists(extracted_exe)) {
        std::cerr << "Error: Extracted binary not found at expected location " << extracted_exe.string() << ".\n";
        return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    }

    // 7. Substituição Atômica (Rename)
    std::cout << "Installing update to " << current_exe.string() << "...\n";
    auto new_exe_temp = exe_dir / (current_exe.filename().string() + ".new");
    std::filesystem::copy_file(extracted_exe, new_exe_temp, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "Error: Failed to copy new binary to target directory: " << ec.message() << "\n";
        std::filesystem::remove(extracted_exe, ec);
        return std::unexpected(ec);
    }
    std::filesystem::remove(extracted_exe, ec);

    // Definir permissões de execução (0755)
    std::filesystem::permissions(new_exe_temp, 
        std::filesystem::perms::owner_all | 
        std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
        std::filesystem::perms::others_read | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::replace, ec);

    // Rename atômico
    std::filesystem::rename(new_exe_temp, current_exe, ec);
    if (ec) {
        std::cerr << "Error: Failed to replace active binary: " << ec.message() << "\n";
        std::filesystem::remove(new_exe_temp, ec);
        return std::unexpected(ec);
    }

    std::cout << "Update successfully installed to " << current_exe.string() << "!\n\n";

    // 8. Exibir Changelog resumido (título dos commits)
    std::cout << "=========================================\n";
    std::cout << "       CHANGELOG FOR " << tag_name << "\n";
    std::cout << "=========================================\n";
    
    // Parse e formata o changelog do release (body)
    std::stringstream body_stream(body);
    std::string line;
    bool has_commits = false;
    while (std::getline(body_stream, line)) {
        // Mostra linhas que descrevem commits ou tópicos principais
        if (line.starts_with("*") || line.starts_with("-") || line.starts_with("###") || line.starts_with("##")) {
            std::cout << line << "\n";
            has_commits = true;
        }
    }
    if (!has_commits && !body.empty()) {
        std::cout << body << "\n";
    } else if (body.empty()) {
        std::cout << "No changelog details provided for this release.\n";
    }
    std::cout << "=========================================\n";

    return {};
}

} // namespace mstorage::core
