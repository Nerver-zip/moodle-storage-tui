#pragma once
#include "core/session_manager.hpp"
#include "storage/upload_history.hpp"
#include "moodle/moodle_client.hpp"
#include "theme.hpp"
#include "models/models.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/screen/screen.hpp>
#include <mutex>
#include <set>
#include <vector>
#include <string>
#include <filesystem>
#include <functional>
#include <memory>

namespace mstorage::tui {

struct LocalFileNode {
    std::filesystem::path path;
    std::string name;
    bool is_directory = false;
    bool is_parent = false;
};

class TuiContext {
public:
    core::SessionManager& session_manager;
    network::HttpClient& http_client;
    storage::HistoryManager& history_manager;
    Theme theme;

    // TUI State
    std::vector<models::MoodleFile> all_files;
    std::vector<models::MoodleFile> files;
    std::vector<std::string> file_names = {"Loading..."};
    models::StorageUsage usage = {0, 100 * 1024 * 1024};
    int selected = 0;
    bool loading = true;
    bool first_load = true;
    std::mutex data_mutex;
    ftxui::ScreenInteractive* screen = nullptr;
    int active_tab = 0; // 0: Browser, 1: Login, 2: Mkdir, 3: Upload, 4: History, 5: Download, 6: Delete, 7: Main Menu, 8: Settings, 9: Themes

    // Multi-selection and collapse sets
    std::set<std::string> selected_paths;
    std::set<std::string> collapsed_folders;

    // exit callback helper
    std::function<void()> exit_cb;

    // Operation callbacks
    std::function<void()> trigger_refresh_cb;
    std::function<void()> perform_login_cb;
    std::function<void()> perform_mkdir_cb;
    std::function<void()> perform_upload_cb;
    std::function<void()> perform_download_cb;
    std::function<void()> perform_delete_cb;

    // Login inputs
    std::string login_url = "https://e-aula.ufpel.edu.br";
    std::string login_username = "";
    std::string login_password = "";
    bool extract_permanent_token = true;
    bool save_keyring = true;
    std::string login_status = "";

    // Mkdir inputs
    std::string mkdir_name = "";
    std::string mkdir_status = "";

    // Upload inputs
    std::string upload_path = "";
    bool upload_recursive = true;
    std::string upload_status = "";
    std::string moodle_upload_path = "/";
    std::filesystem::path current_local_dir;
    std::vector<LocalFileNode> visible_local_nodes;
    std::vector<std::string> local_node_names = {"Loading..."};
    int selected_local_node = 0;
    std::set<std::filesystem::path> expanded_local_dirs;
    std::set<std::filesystem::path> selected_local_paths;

    // Download inputs
    std::string download_path = "";
    std::string download_status = "";

    // Delete status
    std::string delete_status = "";

    // History inputs
    std::vector<std::string> history_entries;
    int history_selected = 0;

    // Main Menu inputs
    std::vector<std::string> main_menu_entries = {"Settings", "Quit"};
    int main_menu_selected = 0;

    // Settings inputs
    std::vector<std::string> settings_entries = {"Themes", "Clear History", "Clear Data"};
    int settings_selected = 0;
    std::string settings_status = "";

    // Themes inputs
    std::vector<std::string> theme_names;
    int theme_selected = 0;

    // Shared components for testing
    ftxui::Component upload_container;
    ftxui::Component local_files_menu;

    TuiContext(core::SessionManager& sm, network::HttpClient& hc, storage::HistoryManager& hm);

    void trigger_refresh();
    void close_dialog();
    std::unique_ptr<moodle::MoodleClient> get_client();
    
    // Local Filesystem helpers
    void update_local_nodes();
    void go_up_local_dir();
    void add_local_directory_contents(const std::filesystem::path& dir, int depth);
    
    // Moodle Filesystem helpers
    void update_visible_files();
    bool is_visible(const models::MoodleFile& file);
    std::string truncate(std::string str, size_t width);
    std::string get_folder_name(const std::string& filepath);

    // Theme helpers
    void refresh_themes_list();
    void apply_selected_theme();
    void ensure_default_themes(const std::filesystem::path& theme_dir);

    // Option builders
    ftxui::ButtonOption make_button_option(bool primary);
    ftxui::MenuOption make_menu_option(std::function<void()> on_enter = nullptr);
};

} // namespace mstorage::tui
