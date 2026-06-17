#pragma once
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include "core/session_manager.hpp"
#include "network/http_client.hpp"
#include "moodle/moodle_client.hpp"
#include "tui/theme.hpp"
#include "storage/upload_history.hpp"
#include <vector>
#include <string>
#include <future>
#include <thread>
#include <format>
#include <mutex>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <set>

namespace mstorage::tui {

class TuiApplication {
public:
    TuiApplication(core::SessionManager& session_manager, network::HttpClient& http_client, storage::HistoryManager& history_manager)
        : session_manager_(session_manager), http_client_(http_client), history_manager_(history_manager) {
        char* home = getenv("HOME");
        std::filesystem::path config_dir = std::string(home ? home : ".") + "/.config/mstorage/themes";
        ensure_default_themes(config_dir);
        
        std::filesystem::path config_path = config_dir / "default.conf";
        theme_ = ThemeManager::load_from_file(config_path);
    }

    void run() {
        auto screen = ftxui::ScreenInteractive::TerminalOutput();
        screen_ = &screen;
        
        auto session = session_manager_.load();
        if (!session) {
            active_tab_ = 1; // Login screen
        } else {
            active_tab_ = 0; // Browser screen
            trigger_refresh();
        }

        auto component = get_root_component(screen.ExitLoopClosure());
        screen.Loop(component);
    }

    ~TuiApplication() {
        if (refresh_thread_.joinable()) {
            refresh_thread_.join();
        }
        if (action_thread_.joinable()) {
            action_thread_.join();
        }
    }

    void trigger_refresh() {
        auto session = session_manager_.load();
        if (!session) return;

        if (refresh_thread_.joinable()) {
            refresh_thread_.join();
        }

        loading_ = true;
        refresh_thread_ = std::thread([this, session]() {
            moodle::MoodleClient client(http_client_, session->moodle_url);
            client.set_wstoken(session->wstoken);
            client.set_web_cookie(session->web_cookie);
            
            std::vector<models::MoodleFile> all_files;
            
            fetch_recursive_all(client, session->web_cookie, "/", all_files);
            auto fuse = client.get_usage(session->web_cookie);
            
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                all_files_ = std::move(all_files);
                if (fuse) usage_ = *fuse;
                loading_ = false;
            }
            if (screen_) {
                screen_->PostEvent(ftxui::Event::Custom);
            }
        });
    }

private:
    void ensure_default_themes(const std::filesystem::path& theme_dir) {
        std::filesystem::create_directories(theme_dir);
        
        struct DefaultTheme {
            std::string name;
            std::string content;
        };
        
        std::vector<DefaultTheme> defaults = {
            {"catppuccin_mocha.conf", 
             "# Catppuccin Mocha\n"
             "theme[main_bg]=\"#1e1e2e\"\ntheme[main_fg]=\"#cdd6f4\"\ntheme[title]=\"#cdd6f4\"\ntheme[hi_fg]=\"#89b4fa\"\n"
             "theme[selected_bg]=\"#45475a\"\ntheme[selected_fg]=\"#89b4fa\"\ntheme[inactive_fg]=\"#7f849c\"\n"
             "theme[cpu_box]=\"#cba6f7\"\ntheme[mem_box]=\"#a6e3a1\"\ntheme[div_line]=\"#6c7086\"\n"
             "theme[used_start]=\"#a6e3a1\"\ntheme[available_mid]=\"#f9e2af\"\ntheme[available_end]=\"#f38ba8\"\n"},
             
            {"catppuccin_macchiato.conf",
             "# Catppuccin Macchiato\n"
             "theme[main_bg]=\"#24273a\"\ntheme[main_fg]=\"#cad3f5\"\ntheme[title]=\"#cad3f5\"\ntheme[hi_fg]=\"#8aadf4\"\n"
             "theme[selected_bg]=\"#363a4f\"\ntheme[selected_fg]=\"#8aadf4\"\ntheme[inactive_fg]=\"#8087a2\"\n"
             "theme[cpu_box]=\"#c6a0f6\"\ntheme[mem_box]=\"#a6da95\"\ntheme[div_line]=\"#6e738d\"\n"
             "theme[used_start]=\"#a6da95\"\ntheme[available_mid]=\"#eed49f\"\ntheme[available_end]=\"#ed8796\"\n"},
             
            {"catppuccin_frappe.conf",
             "# Catppuccin Frappe\n"
             "theme[main_bg]=\"#303446\"\ntheme[main_fg]=\"#c6d0f5\"\ntheme[title]=\"#c6d0f5\"\ntheme[hi_fg]=\"#8caaee\"\n"
             "theme[selected_bg]=\"#414559\"\ntheme[selected_fg]=\"#8caaee\"\ntheme[inactive_fg]=\"#838ba7\"\n"
             "theme[cpu_box]=\"#ca9ee6\"\ntheme[mem_box]=\"#a6d189\"\ntheme[div_line]=\"#737994\"\n"
             "theme[used_start]=\"#a6d189\"\ntheme[available_mid]=\"#e5c890\"\ntheme[available_end]=\"#e78284\"\n"},
             
            {"catppuccin_latte.conf",
             "# Catppuccin Latte\n"
             "theme[main_bg]=\"#eff1f5\"\ntheme[main_fg]=\"#4c4f69\"\ntheme[title]=\"#4c4f69\"\ntheme[hi_fg]=\"#1e66f5\"\n"
             "theme[selected_bg]=\"#ccd0da\"\ntheme[selected_fg]=\"#1e66f5\"\ntheme[inactive_fg]=\"#7c7f93\"\n"
             "theme[cpu_box]=\"#8839ef\"\ntheme[mem_box]=\"#40a02b\"\ntheme[div_line]=\"#9ca0b0\"\n"
             "theme[used_start]=\"#40a02b\"\ntheme[available_mid]=\"#df8e1d\"\ntheme[available_end]=\"#d20f39\"\n"},
             
            {"dracula.conf",
             "# Dracula Theme\n"
             "theme[main_bg]=\"#282a36\"\ntheme[main_fg]=\"#f8f8f2\"\ntheme[title]=\"#f8f8f2\"\ntheme[hi_fg]=\"#8be9fd\"\n"
             "theme[selected_bg]=\"#44475a\"\ntheme[selected_fg]=\"#50fa7b\"\ntheme[inactive_fg]=\"#6272a4\"\n"
             "theme[cpu_box]=\"#bd93f9\"\ntheme[mem_box]=\"#50fa7b\"\ntheme[div_line]=\"#44475a\"\n"
             "theme[used_start]=\"#50fa7b\"\ntheme[available_mid]=\"#ffb86c\"\ntheme[available_end]=\"#ff5555\"\n"},
             
            {"tokyo_night.conf",
             "# Tokyo Night (Storm)\n"
             "theme[main_bg]=\"#24283b\"\ntheme[main_fg]=\"#a9b1d6\"\ntheme[title]=\"#a9b1d6\"\ntheme[hi_fg]=\"#7aa2f7\"\n"
             "theme[selected_bg]=\"#2f3549\"\ntheme[selected_fg]=\"#7aa2f7\"\ntheme[inactive_fg]=\"#565f89\"\n"
             "theme[cpu_box]=\"#bb9af7\"\ntheme[mem_box]=\"#9ece6a\"\ntheme[div_line]=\"#3b4261\"\n"
             "theme[used_start]=\"#9ece6a\"\ntheme[available_mid]=\"#e0af68\"\ntheme[available_end]=\"#f7768e\"\n"},
             
            {"gruvbox_dark.conf",
             "# Gruvbox Dark\n"
             "theme[main_bg]=\"#282828\"\ntheme[main_fg]=\"#ebdbb2\"\ntheme[title]=\"#ebdbb2\"\ntheme[hi_fg]=\"#458588\"\n"
             "theme[selected_bg]=\"#3c3836\"\ntheme[selected_fg]=\"#fabd2f\"\ntheme[inactive_fg]=\"#928374\"\n"
             "theme[cpu_box]=\"#b16286\"\ntheme[mem_box]=\"#b8bb26\"\ntheme[div_line]=\"#504945\"\n"
             "theme[used_start]=\"#b8bb26\"\ntheme[available_mid]=\"#fabd2f\"\ntheme[available_end]=\"#fb4934\"\n"},
             
            {"gruvbox_light.conf",
             "# Gruvbox Light\n"
             "theme[main_bg]=\"#fbf1c7\"\ntheme[main_fg]=\"#3c3836\"\ntheme[title]=\"#3c3836\"\ntheme[hi_fg]=\"#076678\"\n"
             "theme[selected_bg]=\"#ebdbb2\"\ntheme[selected_fg]=\"#b57614\"\ntheme[inactive_fg]=\"#7c6f64\"\n"
             "theme[cpu_box]=\"#8f3f71\"\ntheme[mem_box]=\"#79740e\"\ntheme[div_line]=\"#d5c4a1\"\n"
             "theme[used_start]=\"#79740e\"\ntheme[available_mid]=\"#b57614\"\ntheme[available_end]=\"#9d0006\"\n"},
             
            {"nord.conf",
             "# Nord Theme\n"
             "theme[main_bg]=\"#2e3440\"\ntheme[main_fg]=\"#d8dee9\"\ntheme[title]=\"#e5e9f0\"\ntheme[hi_fg]=\"#88c0d0\"\n"
             "theme[selected_bg]=\"#3b4252\"\ntheme[selected_fg]=\"#88c0d0\"\ntheme[inactive_fg]=\"#4c566a\"\n"
             "theme[cpu_box]=\"#b48ead\"\ntheme[mem_box]=\"#a3be8c\"\ntheme[div_line]=\"#434c5e\"\n"
             "theme[used_start]=\"#a3be8c\"\ntheme[available_mid]=\"#ebcb8b\"\ntheme[available_end]=\"#bf616a\"\n"},
             
            {"one_dark.conf",
             "# One Dark\n"
             "theme[main_bg]=\"#282c34\"\ntheme[main_fg]=\"#abb2bf\"\ntheme[title]=\"#abb2bf\"\ntheme[hi_fg]=\"#61afef\"\n"
             "theme[selected_bg]=\"#3e4452\"\ntheme[selected_fg]=\"#61afef\"\ntheme[inactive_fg]=\"#5c6370\"\n"
             "theme[cpu_box]=\"#c678dd\"\ntheme[mem_box]=\"#98c379\"\ntheme[div_line]=\"#4b5263\"\n"
             "theme[used_start]=\"#98c379\"\ntheme[available_mid]=\"#e5c07b\"\ntheme[available_end]=\"#e06c75\"\n"},
             
            {"solarized_dark.conf",
             "# Solarized Dark\n"
             "theme[main_bg]=\"#002b36\"\ntheme[main_fg]=\"#839496\"\ntheme[title]=\"#93a1a1\"\ntheme[hi_fg]=\"#268bd2\"\n"
             "theme[selected_bg]=\"#073642\"\ntheme[selected_fg]=\"#b58900\"\ntheme[inactive_fg]=\"#586e75\"\n"
             "theme[cpu_box]=\"#d33682\"\ntheme[mem_box]=\"#859900\"\ntheme[div_line]=\"#073642\"\n"
             "theme[used_start]=\"#859900\"\ntheme[available_mid]=\"#b58900\"\ntheme[available_end]=\"#dc322f\"\n"},
             
            {"everforest_dark.conf",
             "# Everforest Dark\n"
             "theme[main_bg]=\"#2b3339\"\ntheme[main_fg]=\"#d3c6aa\"\ntheme[title]=\"#d3c6aa\"\ntheme[hi_fg]=\"#7fbbb3\"\n"
             "theme[selected_bg]=\"#323c41\"\ntheme[selected_fg]=\"#dbbc7f\"\ntheme[inactive_fg]=\"#859289\"\n"
             "theme[cpu_box]=\"#d699b6\"\ntheme[mem_box]=\"#a7c080\"\ntheme[div_line]=\"#3a454a\"\n"
             "theme[used_start]=\"#a7c080\"\ntheme[available_mid]=\"#dbbc7f\"\ntheme[available_end]=\"#e67e80\"\n"}
        };
        
        for (const auto& t : defaults) {
            std::filesystem::path p = theme_dir / t.name;
            if (!std::filesystem::exists(p)) {
                std::ofstream f(p);
                f << t.content;
            }
        }
    }

    void refresh_themes_list() {
        theme_names_.clear();
        char* home = getenv("HOME");
        std::filesystem::path theme_dir = std::string(home ? home : ".") + "/.config/mstorage/themes";
        
        ensure_default_themes(theme_dir);
        
        if (std::filesystem::exists(theme_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(theme_dir)) {
                if (entry.path().extension() == ".conf") {
                    theme_names_.push_back(entry.path().stem().string());
                }
            }
        }
        std::sort(theme_names_.begin(), theme_names_.end());
        if (theme_names_.empty()) {
            theme_names_.push_back("default");
        }
        theme_selected_ = 0;
    }

    void apply_selected_theme() {
        if (theme_selected_ < 0 || theme_selected_ >= static_cast<int>(theme_names_.size())) return;
        std::string theme_name = theme_names_[theme_selected_];
        char* home = getenv("HOME");
        std::filesystem::path theme_path = std::string(home ? home : ".") + "/.config/mstorage/themes/" + theme_name + ".conf";
        theme_ = ThemeManager::load_from_file(theme_path);
        
        std::filesystem::path default_conf_path = std::string(home ? home : ".") + "/.config/mstorage/themes/default.conf";
        std::error_code ec;
        std::filesystem::copy_file(theme_path, default_conf_path, std::filesystem::copy_options::overwrite_existing, ec);
    }

    void open_settings() {
        settings_selected_ = 0;
        settings_status_ = "";
        active_tab_ = 8;
    }

    void open_themes() {
        refresh_themes_list();
        active_tab_ = 9;
    }

    void fetch_recursive_all(moodle::MoodleClient& client, const std::string& cookie, const std::string& path, std::vector<models::MoodleFile>& out_files) {
        auto files = client.list_files(cookie, path);
        if (!files) return;

        for (auto& file : *files) {
            if (file.filename == ".") {
                if (file.filepath != "/") {
                    models::MoodleFile folder_file = file;
                    folder_file.filename = get_folder_name(file.filepath);
                    folder_file.size_f = "DIR";
                    out_files.push_back(folder_file);

                    fetch_recursive_all(client, cookie, file.filepath, out_files);
                }
            } else {
                out_files.push_back(file);
            }
        }
    }

    std::string get_folder_name(const std::string& filepath) {
        if (filepath == "/") return "/";
        std::string clean_path = filepath;
        if (clean_path.back() == '/') clean_path.pop_back();
        auto pos = clean_path.find_last_of('/');
        if (pos != std::string::npos) {
            return clean_path.substr(pos + 1);
        }
        return clean_path;
    }

    std::string truncate(std::string str, size_t width) {
        if (str.length() > width) {
            return str.substr(0, width - 3) + "...";
        }
        return str;
    }

    std::unique_ptr<moodle::MoodleClient> get_client() {
        auto session = session_manager_.load();
        if (!session) return nullptr;
        auto client = std::make_unique<moodle::MoodleClient>(http_client_, session->moodle_url);
        client->set_wstoken(session->wstoken);
        client->set_web_cookie(session->web_cookie);
        return client;
    }

    bool is_visible(const models::MoodleFile& file) {
        for (const auto& collapsed : collapsed_folders_) {
            if (file.filepath.starts_with(collapsed) && file.filepath.length() > collapsed.length()) {
                return false;
            }
            if (file.filepath == collapsed && file.filename != get_folder_name(collapsed)) {
                return false;
            }
        }
        return true;
    }

    void update_visible_files() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        files_.clear();
        file_names_.clear();

        if (all_files_.empty()) {
            if (loading_) {
                file_names_ = {"Loading..."};
            } else {
                file_names_ = {"<No files found>"};
            }
            selected_ = 0;
            return;
        }

        auto term_size = ftxui::Terminal::Size();
        int available_width = term_size.dimx - 45 - 12;
        if (available_width < 15) available_width = 15;

        for (const auto& file : all_files_) {
            if (!is_visible(file)) continue;

            files_.push_back(file);

            int depth = std::count(file.filepath.begin(), file.filepath.end(), '/') - 1;
            if (file.filepath != "/" && file.filepath.back() == '/') depth--;
            if (depth < 0) depth = 0;

            std::string indent(depth * 2, ' ');
            
            std::string select_indicator = "☐ ";
            std::string item_key = file.filepath + "/" + file.filename;
            if (selected_paths_.contains(item_key)) {
                select_indicator = "☑ ";
            }

            std::string prefix = select_indicator;
            if (file.size_f == "DIR") {
                bool is_collapsed = collapsed_folders_.contains(file.filepath);
                prefix += (is_collapsed ? "📁 ▶ " : "📁 ▼ ");
                std::string display = indent + prefix + file.filename;
                file_names_.push_back(truncate(display, available_width));
            } else {
                prefix += "📄 ";
                std::string display = indent + prefix + file.filename;
                file_names_.push_back(truncate(display, available_width));
            }
        }

        if (file_names_.empty()) {
            file_names_.push_back("<No files found>");
            selected_ = 0;
        } else if (selected_ >= static_cast<int>(files_.size())) {
            selected_ = static_cast<int>(files_.size()) - 1;
        }
    }

    ftxui::ButtonOption make_button_option(bool primary) {
        ftxui::ButtonOption opt;
        opt.transform = [this, primary](const ftxui::EntryState& s) {
            auto element = ftxui::text(s.label) | ftxui::center;
            if (s.focused) {
                return element | ftxui::bold | ftxui::color(theme_.selected_fg) | ftxui::bgcolor(theme_.selected_bg) | ftxui::border;
            } else {
                if (primary) {
                    return element | ftxui::color(theme_.main_bg) | ftxui::bgcolor(theme_.box_border) | ftxui::border;
                } else {
                    return element | ftxui::color(theme_.inactive_fg) | ftxui::border;
                }
            }
        };
        return opt;
    }

    ftxui::MenuOption make_menu_option(std::function<void()> on_enter = nullptr) {
        ftxui::MenuOption opt;
        opt.entries_option.transform = [this](const ftxui::EntryState& s) {
            auto element = ftxui::text(s.label);
            if (s.focused) {
                return element | ftxui::bold | ftxui::color(theme_.selected_fg) | ftxui::bgcolor(theme_.selected_bg);
            } else if (s.active) {
                return element | ftxui::bold | ftxui::color(theme_.selected_fg);
            } else {
                return element | ftxui::color(theme_.main_fg);
            }
        };
        if (on_enter) {
            opt.on_enter = on_enter;
        }
        return opt;
    }

    void perform_login() {
        if (login_url_.empty() || login_username_.empty() || login_password_.empty()) {
            login_status_ = "Please fill in all fields.";
            return;
        }

        login_status_ = "Authenticating with Moodle...";

        if (action_thread_.joinable()) action_thread_.join();
        action_thread_ = std::thread([this]() {
            moodle::ShibbolethAuth auth(login_url_);
            auto login_res = auth.login_web(login_username_, login_password_);
            if (!login_res) {
                login_status_ = "Login failed: " + login_res.error().message();
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            std::string session_cookie = *login_res;
            std::string token = "";

            if (extract_permanent_token_) {
                login_status_ = "Extracting permanent token...";
                screen_->PostEvent(ftxui::Event::Custom);
                
                auto token_res = auth.extract_mobile_token(session_cookie);
                if (!token_res) {
                    login_status_ = "Failed to extract mobile token.";
                    screen_->PostEvent(ftxui::Event::Custom);
                    return;
                }
                token = *token_res;
            }

            models::SessionData data {
                .moodle_url = login_url_,
                .wstoken = token,
                .web_cookie = session_cookie
            };

            std::expected<void, std::error_code> save_res;
            if (save_keyring_) {
                save_res = session_manager_.save(data, login_username_, login_password_);
            } else {
                save_res = session_manager_.save(data);
            }

            if (!save_res) {
                login_status_ = "Failed to save session securely.";
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            login_status_ = "";
            login_username_ = "";
            login_password_ = "";
            active_tab_ = 0; // go back to browser
            trigger_refresh();
            screen_->PostEvent(ftxui::Event::Custom);
        });
    }

    void perform_mkdir() {
        if (mkdir_name_.empty()) {
            mkdir_status_ = "Please enter a folder name.";
            return;
        }

        mkdir_status_ = "Creating folder...";

        if (action_thread_.joinable()) action_thread_.join();
        action_thread_ = std::thread([this]() {
            auto client = get_client();
            if (!client) {
                mkdir_status_ = "Error: client not initialized.";
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            auto session = session_manager_.load();
            auto draft_info = client->get_draft_info(session->web_cookie);
            if (!draft_info) {
                mkdir_status_ = "Failed to get draft info: " + draft_info.error().message();
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            std::string parent_path = "/";
            if (!files_.empty() && selected_ < static_cast<int>(files_.size())) {
                const auto& f = files_[selected_];
                if (f.size_f == "DIR") {
                    parent_path = f.filepath;
                } else {
                    parent_path = f.filepath;
                }
            }

            if (parent_path.empty() || parent_path.front() != '/') parent_path = "/" + parent_path;
            if (parent_path.back() != '/') parent_path += "/";

            auto res = client->create_folder(mkdir_name_, parent_path, *draft_info, session->web_cookie);
            if (!res) {
                mkdir_status_ = "Failed to create folder: " + res.error().message();
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            auto commit_res = client->commit_draft(*draft_info, session->web_cookie);
            if (!commit_res) {
                mkdir_status_ = "Failed to commit changes: " + commit_res.error().message();
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            mkdir_status_ = "";
            mkdir_name_ = "";
            active_tab_ = 0; // Go back to browser
            trigger_refresh();
            screen_->PostEvent(ftxui::Event::Custom);
        });
    }

    void perform_upload() {
        if (upload_path_.empty()) {
            upload_status_ = "Please enter a local file/folder path.";
            return;
        }

        if (!std::filesystem::exists(upload_path_)) {
            upload_status_ = "Local path does not exist.";
            return;
        }

        upload_status_ = "Uploading...";

        if (action_thread_.joinable()) action_thread_.join();
        action_thread_ = std::thread([this]() {
            auto client = get_client();
            if (!client) {
                upload_status_ = "Error: client not initialized.";
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            auto session = session_manager_.load();
            auto draft_info = client->get_draft_info(session->web_cookie);
            if (!draft_info) {
                upload_status_ = "Failed to get draft info: " + draft_info.error().message();
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            std::string parent_path = "/";
            if (!files_.empty() && selected_ < static_cast<int>(files_.size())) {
                const auto& f = files_[selected_];
                if (f.size_f == "DIR") {
                    parent_path = f.filepath;
                } else {
                    parent_path = f.filepath;
                }
            }
            if (parent_path.empty() || parent_path.front() != '/') parent_path = "/" + parent_path;
            if (parent_path.back() != '/') parent_path += "/";

            auto upload_single_file = [&](const std::string& local_path, const std::string& remote_dir) -> std::expected<void, std::error_code> {
                auto res = client->upload_file(local_path, remote_dir, *draft_info, session->web_cookie);
                if (res) {
                    (void)history_manager_.record_upload(std::filesystem::path(local_path).filename().string(), "TUI");
                }
                return res;
            };

            std::function<std::expected<void, std::error_code>(const std::filesystem::path&, const std::string&)> upload_recursive = 
                [&](const std::filesystem::path& local_dir, const std::string& remote_parent) -> std::expected<void, std::error_code> {
                    std::string folder_name = local_dir.filename().string();
                    std::string remote_dir = remote_parent + folder_name + "/";

                    auto mkdir_res = client->create_folder(folder_name, remote_parent, *draft_info, session->web_cookie);
                    if (!mkdir_res) return std::unexpected(mkdir_res.error());

                    for (const auto& entry : std::filesystem::directory_iterator(local_dir)) {
                        if (entry.is_directory()) {
                            auto res = upload_recursive(entry.path(), remote_dir);
                            if (!res) return std::unexpected(res.error());
                        } else {
                            auto res = upload_single_file(entry.path().string(), remote_dir);
                            if (!res) return std::unexpected(res.error());
                        }
                    }
                    return {};
                };

            std::expected<void, std::error_code> res;
            if (std::filesystem::is_directory(upload_path_)) {
                if (upload_recursive_) {
                    res = upload_recursive(upload_path_, parent_path);
                } else {
                    upload_status_ = "Skipping folder upload. Check 'Recursive' checkbox.";
                    screen_->PostEvent(ftxui::Event::Custom);
                    return;
                }
            } else {
                res = upload_single_file(upload_path_, parent_path);
            }

            if (!res) {
                upload_status_ = "Upload failed: " + res.error().message();
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            auto commit_res = client->commit_draft(*draft_info, session->web_cookie);
            if (!commit_res) {
                upload_status_ = "Commit failed: " + commit_res.error().message();
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            upload_status_ = "";
            upload_path_ = "";
            active_tab_ = 0; // Go back to browser
            trigger_refresh();
            screen_->PostEvent(ftxui::Event::Custom);
        });
    }

    void perform_download() {
        if (download_path_.empty()) {
            download_status_ = "Please enter a target path.";
            return;
        }

        download_status_ = "Downloading...";

        if (action_thread_.joinable()) action_thread_.join();
        action_thread_ = std::thread([this]() {
            auto client = get_client();
            if (!client) {
                download_status_ = "Error: client not initialized.";
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            auto session = session_manager_.load();

            std::vector<models::MoodleFile> items_to_download;
            if (!selected_paths_.empty()) {
                for (const auto& file : all_files_) {
                    std::string item_key = file.filepath + "/" + file.filename;
                    if (selected_paths_.contains(item_key)) {
                        items_to_download.push_back(file);
                    }
                }
            } else {
                if (files_.empty() || selected_ >= static_cast<int>(files_.size())) {
                    download_status_ = "No file selected.";
                    screen_->PostEvent(ftxui::Event::Custom);
                    return;
                }
                items_to_download.push_back(files_[selected_]);
            }

            std::expected<void, std::error_code> res;
            for (const auto& file : items_to_download) {
                download_status_ = "Downloading: " + (file.size_f == "DIR" ? file.filepath : file.filename);
                screen_->PostEvent(ftxui::Event::Custom);

                if (file.size_f == "DIR") {
                    auto draft_info = client->get_draft_info(session->web_cookie);
                    if (draft_info) {
                        auto zip_url = client->zip_folder(file.filepath, *draft_info, session->web_cookie);
                        if (zip_url) {
                            std::string target_file = download_path_;
                            if (std::filesystem::is_directory(target_file)) {
                                target_file = (std::filesystem::path(target_file) / (get_folder_name(file.filepath) + ".zip")).string();
                            }
                            res = client->download_file(*zip_url, target_file, session->web_cookie);
                        } else {
                            res = std::unexpected(std::make_error_code(std::errc::not_supported));
                        }
                    } else {
                        res = std::unexpected(std::make_error_code(std::errc::permission_denied));
                    }
                } else {
                    std::string target_file = download_path_;
                    if (std::filesystem::is_directory(target_file)) {
                        target_file = (std::filesystem::path(target_file) / file.filename).string();
                    }
                    res = client->download_file(file.url, target_file, session->web_cookie);
                }

                if (!res) {
                    download_status_ = "Download failed for " + file.filename + ": " + res.error().message();
                    screen_->PostEvent(ftxui::Event::Custom);
                    return;
                }
            }

            selected_paths_.clear();
            download_status_ = "";
            active_tab_ = 0; // Go back to browser
            screen_->PostEvent(ftxui::Event::Custom);
        });
    }

    void perform_delete() {
        delete_status_ = "Deleting...";

        if (action_thread_.joinable()) action_thread_.join();
        action_thread_ = std::thread([this]() {
            auto client = get_client();
            if (!client) {
                delete_status_ = "Error: client not initialized.";
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            auto session = session_manager_.load();
            std::string active_cookie = session->web_cookie;
            
            auto draft_info = client->get_draft_info(active_cookie);
            if (!draft_info) {
                auto creds = session_manager_.load_credentials(session->moodle_url);
                if (creds && !creds->username.empty() && !creds->password.empty()) {
                    auto refreshed = client->refresh_web_session(creds->username, creds->password);
                    if (refreshed) {
                        active_cookie = *refreshed;
                        session->web_cookie = active_cookie;
                        (void)session_manager_.save(*session);
                        draft_info = client->get_draft_info(active_cookie);
                    }
                }
            }

            if (!draft_info) {
                draft_info = client->get_draft_info();
            }

            if (!draft_info) {
                delete_status_ = "Failed to get draft info: " + draft_info.error().message();
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            std::vector<models::DeleteItem> items;
            if (!selected_paths_.empty()) {
                for (const auto& file : all_files_) {
                    std::string item_key = file.filepath + "/" + file.filename;
                    if (selected_paths_.contains(item_key)) {
                        if (file.size_f == "DIR") {
                            std::string folder_path = file.filepath;
                            if (folder_path.back() == '/') folder_path.pop_back();
                            auto pos = folder_path.find_last_of('/');
                            std::string parent = folder_path.substr(0, pos + 1);
                            std::string name = folder_path.substr(pos + 1);
                            items.push_back({name, parent, true});
                        } else {
                            items.push_back({file.filename, file.filepath, false});
                        }
                    }
                }
            } else {
                if (files_.empty() || selected_ >= static_cast<int>(files_.size())) {
                    delete_status_ = "No file selected.";
                    screen_->PostEvent(ftxui::Event::Custom);
                    return;
                }
                const auto& file = files_[selected_];
                if (file.size_f == "DIR") {
                    std::string folder_path = file.filepath;
                    if (folder_path.back() == '/') folder_path.pop_back();
                    auto pos = folder_path.find_last_of('/');
                    std::string parent = folder_path.substr(0, pos + 1);
                    std::string name = folder_path.substr(pos + 1);
                    items.push_back({name, parent, true});
                } else {
                    items.push_back({file.filename, file.filepath, false});
                }
            }

            if (items.empty()) {
                delete_status_ = "No items to delete.";
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            auto delete_res = client->delete_items(items, *draft_info, active_cookie);
            if (!delete_res) {
                delete_status_ = "Delete failed: " + delete_res.error().message();
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            auto commit_res = client->commit_draft(*draft_info, active_cookie);
            if (!commit_res) {
                delete_status_ = "Commit failed: " + commit_res.error().message();
                screen_->PostEvent(ftxui::Event::Custom);
                return;
            }

            selected_paths_.clear();
            delete_status_ = "";
            active_tab_ = 0; // Go back to browser
            trigger_refresh();
            screen_->PostEvent(ftxui::Event::Custom);
        });
    }

    void open_history() {
        history_entries_.clear();
        auto history_res = history_manager_.get_history(100);
        if (history_res) {
            for (const auto& entry : *history_res) {
                history_entries_.push_back(std::format("{} -> {} ({})", entry.filename, entry.url, entry.timestamp));
            }
        }
        if (history_entries_.empty()) {
            history_entries_.push_back("<No history found>");
        }
        history_selected_ = 0;
        active_tab_ = 4;
    }

    void close_dialog() {
        active_tab_ = 0;
        mkdir_name_ = "";
        mkdir_status_ = "";
        upload_path_ = "";
        upload_status_ = "";
        download_path_ = "";
        download_status_ = "";
        delete_status_ = "";
        settings_status_ = "";
    }

public:
    ftxui::Component get_root_component(std::function<void()> exit_callback = [](){}) {
        exit_cb_ = exit_callback;

        // --- Login Screen Components ---
        input_url_ = ftxui::Input(&login_url_, "https://...");
        input_username_ = ftxui::Input(&login_username_, "Username (CPF)");
        
        ftxui::InputOption password_opt = ftxui::InputOption::Default();
        password_opt.password = true;
        input_password_ = ftxui::Input(&login_password_, "Password", password_opt);

        chk_perm_token_ = ftxui::Checkbox("Extract Permanent Mobile Token", &extract_permanent_token_);
        chk_save_keyring_ = ftxui::Checkbox("Save Credentials in Keyring", &save_keyring_);

        btn_login_ = ftxui::Button("Login", [this]() {
            perform_login();
        }, make_button_option(true));

        btn_login_exit_ = ftxui::Button("Exit", exit_callback, make_button_option(false));

        login_container_ = ftxui::Container::Vertical({
            input_url_,
            input_username_,
            input_password_,
            chk_perm_token_,
            chk_save_keyring_,
            btn_login_,
            btn_login_exit_
        });

        // --- Mkdir Dialog Components ---
        input_mkdir_ = ftxui::Input(&mkdir_name_, "Folder Name");
        btn_mkdir_ok_ = ftxui::Button("Create", [this]() {
            perform_mkdir();
        }, make_button_option(true));
        btn_mkdir_cancel_ = ftxui::Button("Cancel", [this]() {
            close_dialog();
        }, make_button_option(false));
        mkdir_container_ = ftxui::Container::Vertical({
            input_mkdir_,
            btn_mkdir_ok_,
            btn_mkdir_cancel_
        });

        // --- Upload Dialog Components ---
        input_upload_ = ftxui::Input(&upload_path_, "Local file/folder path");
        chk_upload_recursive_ = ftxui::Checkbox("Recursive (for directories)", &upload_recursive_);
        btn_upload_ok_ = ftxui::Button("Upload", [this]() {
            perform_upload();
        }, make_button_option(true));
        btn_upload_cancel_ = ftxui::Button("Cancel", [this]() {
            close_dialog();
        }, make_button_option(false));
        upload_container_ = ftxui::Container::Vertical({
            input_upload_,
            chk_upload_recursive_,
            btn_upload_ok_,
            btn_upload_cancel_
        });

        // --- Download Dialog Components ---
        input_download_ = ftxui::Input(&download_path_, "Target local path");
        btn_download_ok_ = ftxui::Button("Download", [this]() {
            perform_download();
        }, make_button_option(true));
        btn_download_cancel_ = ftxui::Button("Cancel", [this]() {
            close_dialog();
        }, make_button_option(false));
        download_container_ = ftxui::Container::Vertical({
            input_download_,
            btn_download_ok_,
            btn_download_cancel_
        });

        // --- Delete Dialog Components ---
        btn_delete_ok_ = ftxui::Button("Delete", [this]() {
            perform_delete();
        }, make_button_option(true));
        btn_delete_cancel_ = ftxui::Button("Cancel", [this]() {
            close_dialog();
        }, make_button_option(false));
        delete_container_ = ftxui::Container::Horizontal({
            btn_delete_ok_,
            btn_delete_cancel_
        });

        // --- History Dialog Components ---
        history_menu_ = ftxui::Menu(&history_entries_, &history_selected_, make_menu_option());
        btn_history_close_ = ftxui::Button("Close", [this]() {
            close_dialog();
        }, make_button_option(false));
        history_container_ = ftxui::Container::Vertical({
            history_menu_,
            btn_history_close_
        });

        // --- Main Menu Dialog Components ---
        main_menu_ = ftxui::Menu(&main_menu_entries_, &main_menu_selected_, make_menu_option([this]() {
            if (main_menu_selected_ == 0) {
                open_settings();
            } else if (main_menu_selected_ == 1) {
                if (exit_cb_) exit_cb_();
            }
        }));
        btn_main_menu_ok_ = ftxui::Button("Select", [this]() {
            if (main_menu_selected_ == 0) {
                open_settings();
            } else if (main_menu_selected_ == 1) {
                if (exit_cb_) exit_cb_();
            }
        }, make_button_option(true));
        btn_main_menu_cancel_ = ftxui::Button("Cancel", [this]() {
            close_dialog();
        }, make_button_option(false));
        main_menu_container_ = ftxui::Container::Vertical({
            main_menu_,
            btn_main_menu_ok_,
            btn_main_menu_cancel_
        });

        // --- Settings Dialog Components ---
        settings_menu_ = ftxui::Menu(&settings_entries_, &settings_selected_, make_menu_option([this]() {
            if (settings_selected_ == 0) {
                open_themes();
            } else if (settings_selected_ == 1) {
                auto res = history_manager_.clear();
                if (res) {
                    settings_status_ = "History cleared successfully.";
                    history_entries_.clear();
                    history_entries_.push_back("<No history found>");
                    history_selected_ = 0;
                } else {
                    settings_status_ = "Failed to clear history.";
                }
            } else if (settings_selected_ == 2) {
                auto res = session_manager_.clear_credentials();
                if (res) {
                    settings_status_ = "Credentials and session cleared.";
                    all_files_.clear();
                    files_.clear();
                    file_names_ = {"Loading..."};
                    selected_ = 0;
                    selected_paths_.clear();
                    active_tab_ = 1;
                } else {
                    settings_status_ = "Failed to clear data.";
                }
            }
        }));
        btn_settings_ok_ = ftxui::Button("Select", [this]() {
            if (settings_selected_ == 0) {
                open_themes();
            } else if (settings_selected_ == 1) {
                auto res = history_manager_.clear();
                if (res) {
                    settings_status_ = "History cleared successfully.";
                    history_entries_.clear();
                    history_entries_.push_back("<No history found>");
                    history_selected_ = 0;
                } else {
                    settings_status_ = "Failed to clear history.";
                }
            } else if (settings_selected_ == 2) {
                auto res = session_manager_.clear_credentials();
                if (res) {
                    settings_status_ = "Credentials and session cleared.";
                    all_files_.clear();
                    files_.clear();
                    file_names_ = {"Loading..."};
                    selected_ = 0;
                    selected_paths_.clear();
                    active_tab_ = 1;
                } else {
                    settings_status_ = "Failed to clear data.";
                }
            }
        }, make_button_option(true));
        btn_settings_cancel_ = ftxui::Button("Back", [this]() {
            active_tab_ = 7;
        }, make_button_option(false));
        settings_container_ = ftxui::Container::Vertical({
            settings_menu_,
            btn_settings_ok_,
            btn_settings_cancel_
        });

        // --- Themes Dialog Components ---
        theme_menu_ = ftxui::Menu(&theme_names_, &theme_selected_, make_menu_option([this]() {
            apply_selected_theme();
        }));
        btn_theme_ok_ = ftxui::Button("Apply", [this]() {
            apply_selected_theme();
        }, make_button_option(true));
        btn_theme_cancel_ = ftxui::Button("Back", [this]() {
            active_tab_ = 8;
        }, make_button_option(false));
        theme_container_ = ftxui::Container::Vertical({
            theme_menu_,
            btn_theme_ok_,
            btn_theme_cancel_
        });

        // --- Browser Components ---
        file_menu_ = ftxui::Menu(&file_names_, &selected_, make_menu_option());
        
        // Main Tab Container
        tab_container_ = ftxui::Container::Tab({
            file_menu_,          // 0: Browser
            login_container_,    // 1: Login
            mkdir_container_,    // 2: Mkdir
            upload_container_,   // 3: Upload
            history_container_,  // 4: History
            download_container_, // 5: Download
            delete_container_,   // 6: Delete
            main_menu_container_,// 7: Main Menu
            settings_container_, // 8: Settings
            theme_container_     // 9: Themes
        }, &active_tab_);

        auto renderer = ftxui::Renderer(tab_container_, [this] {
            update_visible_files();

            auto session = session_manager_.load();
            std::string moodle_url = session ? session->moodle_url : "Unknown";

            if (active_tab_ == 1) {
                // Render Login Screen
                auto login_box = ftxui::window(ftxui::text(" Moodle Storage Authentication ") | ftxui::bold | ftxui::color(theme_.title), 
                    ftxui::vbox({
                        ftxui::hbox(ftxui::text("Moodle URL: ") | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 12), input_url_->Render() | ftxui::border | ftxui::flex),
                        ftxui::hbox(ftxui::text("Username:   ") | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 12), input_username_->Render() | ftxui::border | ftxui::flex),
                        ftxui::hbox(ftxui::text("Password:   ") | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 12), input_password_->Render() | ftxui::border | ftxui::flex),
                        ftxui::separator() | ftxui::color(theme_.div_line),
                        chk_perm_token_->Render(),
                        chk_save_keyring_->Render(),
                        ftxui::separator() | ftxui::color(theme_.div_line),
                        ftxui::hbox({
                            btn_login_->Render(),
                            ftxui::text("   "),
                            btn_login_exit_->Render()
                        }) | ftxui::center,
                        ftxui::text(login_status_) | ftxui::color(theme_.progress_high) | ftxui::center
                    })
                ) | ftxui::color(theme_.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 60) | ftxui::center;
                
                return ftxui::vbox({
                    ftxui::text(" 󰊄 Moodle Storage ") | ftxui::bold | ftxui::color(theme_.title) | ftxui::bgcolor(theme_.selected_bg),
                    ftxui::filler(),
                    login_box,
                    ftxui::filler()
                }) | ftxui::bgcolor(theme_.main_bg);
            }

            // Render Browser Screen
            std::string status_text = "Connected: " + moodle_url;
            if (!selected_paths_.empty()) {
                status_text += std::format(" | Selected: {}", selected_paths_.size());
            }
            
            auto header = ftxui::hbox({
                ftxui::text(" 󰊄 Moodle Storage ") | ftxui::bold | ftxui::color(theme_.title),
                ftxui::filler(),
                ftxui::text(" " + status_text + " ") | ftxui::color(theme_.hi_fg),
            }) | ftxui::bgcolor(theme_.selected_bg);

            auto footer = ftxui::hbox({
                ftxui::text(" [q] Quit ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [r] Refresh ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [u] Upload ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [Enter] Download ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [d] Delete ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [n] Mkdir ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [h] History ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [Space] Select ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [◀/▶] Collapse/Expand ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [s] Settings ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [m] Menu ") | ftxui::color(theme_.hi_fg),
                ftxui::filler(),
                ftxui::text(loading_ ? " UPDATING... " : " READY ") | ftxui::bold | ftxui::color(loading_ ? theme_.hi_fg : theme_.secondary_box),
            });

            auto browser_box = ftxui::window(ftxui::text(" Files "), file_menu_->Render()) 
                             | ftxui::color(theme_.box_border) | ftxui::flex;

            double usage_percent = (static_cast<double>(usage_.used_bytes) / usage_.total_bytes);
            auto usage_color = usage_percent > 0.9 ? theme_.progress_high : (usage_percent > 0.7 ? theme_.progress_mid : theme_.progress_low);

            ftxui::Element details_content;
            if (loading_ && (file_names_.empty() || file_names_[0] == "Loading...")) {
                details_content = ftxui::center(ftxui::text("Loading data...") | ftxui::color(theme_.hi_fg));
            } else if (files_.empty()) {
                details_content = ftxui::center(ftxui::text("No file selected") | ftxui::dim);
            } else {
                std::lock_guard<std::mutex> lock(data_mutex_);
                if (selected_ < static_cast<int>(files_.size())) {
                    const auto& f = files_[selected_];
                    details_content = ftxui::vbox({
                        ftxui::text("Name: " + f.filename) | ftxui::bold | ftxui::color(theme_.main_fg),
                        ftxui::text("Path: " + f.filepath) | ftxui::color(theme_.inactive_fg),
                        ftxui::separator() | ftxui::color(theme_.div_line),
                        ftxui::text("Size: " + f.size_f) | ftxui::color(theme_.main_fg),
                        ftxui::text("Modified: " + std::string(ctime(&f.datemodified))) | ftxui::color(theme_.inactive_fg),
                        ftxui::filler(),
                        ftxui::text("Storage Usage") | ftxui::color(theme_.title),
                        ftxui::gauge(usage_percent) | ftxui::color(usage_color) | ftxui::borderEmpty,
                        ftxui::text(std::format("{:.2f}MB / 100MB ({:.1f}%)", 
                            usage_.used_bytes / (1024.0 * 1024.0), usage_percent * 100.0)) | ftxui::center | ftxui::color(theme_.main_fg),
                    });
                } else {
                    details_content = ftxui::center(ftxui::text("Selection out of bounds") | ftxui::color(theme_.progress_high));
                }
            }

            auto details_box = ftxui::window(ftxui::text(" Details "), details_content) 
                             | ftxui::color(theme_.secondary_box) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 45);

            auto main_layout = ftxui::vbox({
                header,
                ftxui::hbox({
                    browser_box,
                    details_box,
                }) | ftxui::flex,
                footer,
            }) | ftxui::bgcolor(theme_.main_bg);

            // Overlay dialogs
            if (active_tab_ == 2) {
                auto dialog = ftxui::window(ftxui::text(" Create Directory ") | ftxui::bold | ftxui::color(theme_.title),
                    ftxui::vbox({
                        ftxui::text("Folder Name:"),
                        input_mkdir_->Render() | ftxui::border,
                        ftxui::separator() | ftxui::color(theme_.div_line),
                        ftxui::hbox({
                            btn_mkdir_ok_->Render(),
                            ftxui::text("   "),
                            btn_mkdir_cancel_->Render()
                        }) | ftxui::center,
                        ftxui::text(mkdir_status_) | ftxui::color(theme_.hi_fg) | ftxui::center
                    })
                ) | ftxui::color(theme_.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 40) | ftxui::center;
                return ftxui::dbox({ main_layout, ftxui::clear_under(dialog) });
            }

            if (active_tab_ == 3) {
                auto dialog = ftxui::window(ftxui::text(" Upload File/Folder ") | ftxui::bold | ftxui::color(theme_.title),
                    ftxui::vbox({
                        ftxui::text("Local Path:"),
                        input_upload_->Render() | ftxui::border,
                        chk_upload_recursive_->Render(),
                        ftxui::separator() | ftxui::color(theme_.div_line),
                        ftxui::hbox({
                            btn_upload_ok_->Render(),
                            ftxui::text("   "),
                            btn_upload_cancel_->Render()
                        }) | ftxui::center,
                        ftxui::text(upload_status_) | ftxui::color(theme_.hi_fg) | ftxui::center
                    })
                ) | ftxui::color(theme_.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 50) | ftxui::center;
                return ftxui::dbox({ main_layout, ftxui::clear_under(dialog) });
            }

            if (active_tab_ == 4) {
                auto dialog = ftxui::window(ftxui::text(" Upload History ") | ftxui::bold | ftxui::color(theme_.title),
                    ftxui::vbox({
                        history_menu_->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 15),
                        ftxui::separator() | ftxui::color(theme_.div_line),
                        btn_history_close_->Render() | ftxui::center
                    })
                ) | ftxui::color(theme_.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 100) | ftxui::center;
                return ftxui::dbox({ main_layout, ftxui::clear_under(dialog) });
            }

            if (active_tab_ == 5) {
                std::string title_txt = selected_paths_.empty() ? " Download File/Folder " : std::format(" Download {} Selected Items ", selected_paths_.size());
                auto dialog = ftxui::window(ftxui::text(title_txt) | ftxui::bold | ftxui::color(theme_.title),
                    ftxui::vbox({
                        ftxui::text("Local Destination Path:"),
                        input_download_->Render() | ftxui::border,
                        ftxui::separator() | ftxui::color(theme_.div_line),
                        ftxui::hbox({
                            btn_download_ok_->Render(),
                            ftxui::text("   "),
                            btn_download_cancel_->Render()
                        }) | ftxui::center,
                        ftxui::text(download_status_) | ftxui::color(theme_.hi_fg) | ftxui::center
                    })
                ) | ftxui::color(theme_.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 50) | ftxui::center;
                return ftxui::dbox({ main_layout, ftxui::clear_under(dialog) });
            }

            if (active_tab_ == 6) {
                std::string target_msg = "";
                if (!selected_paths_.empty()) {
                    target_msg = std::format("{} selected items", selected_paths_.size());
                } else if (!files_.empty() && selected_ < static_cast<int>(files_.size())) {
                    const auto& f = files_[selected_];
                    target_msg = f.size_f == "DIR" ? "'" + get_folder_name(f.filepath) + "'" : "'" + f.filename + "'";
                }
                auto dialog = ftxui::window(ftxui::text(" Confirm Deletion ") | ftxui::bold | ftxui::color(theme_.title),
                    ftxui::vbox({
                        ftxui::text("Are you sure you want to delete:") | ftxui::center,
                        ftxui::text(target_msg) | ftxui::bold | ftxui::center,
                        ftxui::separator() | ftxui::color(theme_.div_line),
                        ftxui::hbox({
                            btn_delete_ok_->Render(),
                            ftxui::text("   "),
                            btn_delete_cancel_->Render()
                        }) | ftxui::center,
                        ftxui::text(delete_status_) | ftxui::color(theme_.progress_high) | ftxui::center
                    })
                ) | ftxui::color(theme_.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 50) | ftxui::center;
                return ftxui::dbox({ main_layout, ftxui::clear_under(dialog) });
            }

            if (active_tab_ == 7) {
                // Main Menu Dialog
                auto dialog = ftxui::window(ftxui::text(" Main Menu ") | ftxui::bold | ftxui::color(theme_.title),
                    ftxui::vbox({
                        main_menu_->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 3),
                        ftxui::separator() | ftxui::color(theme_.div_line),
                        ftxui::hbox({
                            btn_main_menu_ok_->Render(),
                            ftxui::text("   "),
                            btn_main_menu_cancel_->Render()
                        }) | ftxui::center
                    })
                ) | ftxui::color(theme_.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 40) | ftxui::center;
                return ftxui::dbox({ main_layout, ftxui::clear_under(dialog) });
            }

            if (active_tab_ == 8) {
                // Settings Dialog
                auto dialog = ftxui::window(ftxui::text(" Settings ") | ftxui::bold | ftxui::color(theme_.title),
                    ftxui::vbox({
                        settings_menu_->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 3),
                        ftxui::separator() | ftxui::color(theme_.div_line),
                        ftxui::hbox({
                            btn_settings_ok_->Render(),
                            ftxui::text("   "),
                            btn_settings_cancel_->Render()
                        }) | ftxui::center,
                        ftxui::text(settings_status_) | ftxui::color(theme_.progress_high) | ftxui::center
                    })
                ) | ftxui::color(theme_.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 40) | ftxui::center;
                return ftxui::dbox({ main_layout, ftxui::clear_under(dialog) });
            }

            if (active_tab_ == 9) {
                // Themes Dialog
                auto dialog = ftxui::window(ftxui::text(" Select Theme ") | ftxui::bold | ftxui::color(theme_.title),
                    ftxui::vbox({
                        theme_menu_->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 10),
                        ftxui::separator() | ftxui::color(theme_.div_line),
                        ftxui::hbox({
                            btn_theme_ok_->Render(),
                            ftxui::text("   "),
                            btn_theme_cancel_->Render()
                        }) | ftxui::center
                    })
                ) | ftxui::color(theme_.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 50) | ftxui::center;
                return ftxui::dbox({ main_layout, ftxui::clear_under(dialog) });
            }

            return main_layout;
        });

        return ftxui::CatchEvent(renderer, [this, exit_callback](ftxui::Event event) {
            if (active_tab_ == 1) {
                if (event == ftxui::Event::Character('q')) {
                    exit_callback();
                    return true;
                }
                return false;
            }

            if (active_tab_ != 0) {
                if (event == ftxui::Event::Escape) {
                    close_dialog();
                    return true;
                }
                return false;
            }

            // Browser mode hotkeys
            if (event == ftxui::Event::Character('q')) {
                exit_callback();
                return true;
            }
            if (event == ftxui::Event::Character('r')) {
                trigger_refresh();
                return true;
            }
            if (event == ftxui::Event::Character('u')) {
                upload_path_ = "";
                upload_recursive_ = false;
                upload_status_ = "";
                active_tab_ = 3; // Upload Dialog
                return true;
            }
            if (event == ftxui::Event::Character('d')) {
                if (!selected_paths_.empty() || (!files_.empty() && selected_ < static_cast<int>(files_.size()))) {
                    delete_status_ = "";
                    active_tab_ = 6; // Delete Dialog
                }
                return true;
            }
            if (event == ftxui::Event::Character('h')) {
                open_history();
                return true;
            }
            if (event == ftxui::Event::Character('n')) {
                mkdir_name_ = "";
                mkdir_status_ = "";
                active_tab_ = 2; // Mkdir Dialog (now bound to 'n' for new folder)
                return true;
            }
            if (event == ftxui::Event::Character('s')) {
                open_settings();
                return true;
            }
            if (event == ftxui::Event::Character('m')) {
                active_tab_ = 7;
                main_menu_selected_ = 0;
                return true;
            }
            if (event == ftxui::Event::Character(' ')) {
                if (!files_.empty() && selected_ < static_cast<int>(files_.size())) {
                    const auto& file = files_[selected_];
                    std::string item_key = file.filepath + "/" + file.filename;
                    if (selected_paths_.contains(item_key)) {
                        selected_paths_.erase(item_key);
                    } else {
                        selected_paths_.insert(item_key);
                    }
                    return true;
                }
            }
            if (event == ftxui::Event::ArrowRight) {
                if (!files_.empty() && selected_ < static_cast<int>(files_.size())) {
                    const auto& file = files_[selected_];
                    if (file.size_f == "DIR") {
                        collapsed_folders_.erase(file.filepath);
                        return true;
                    }
                }
            }
            if (event == ftxui::Event::ArrowLeft) {
                if (!files_.empty() && selected_ < static_cast<int>(files_.size())) {
                    const auto& file = files_[selected_];
                    if (file.size_f == "DIR") {
                        collapsed_folders_.insert(file.filepath);
                        return true;
                    }
                }
            }
            if (event == ftxui::Event::Return) {
                if (!selected_paths_.empty()) {
                    download_path_ = ".";
                    download_status_ = "";
                    active_tab_ = 5; // Download Dialog
                    return true;
                }
                if (!files_.empty() && selected_ < static_cast<int>(files_.size())) {
                    const auto& file = files_[selected_];
                    if (file.size_f == "DIR") {
                        download_path_ = get_folder_name(file.filepath) + ".zip";
                    } else {
                        download_path_ = file.filename;
                    }
                    download_status_ = "";
                    active_tab_ = 5; // Download Dialog
                }
                return true;
            }
            return false;
        });
    }

private:
    core::SessionManager& session_manager_;
    network::HttpClient& http_client_;
    storage::HistoryManager& history_manager_;
    Theme theme_;

    // TUI State
    std::vector<models::MoodleFile> all_files_;
    std::vector<models::MoodleFile> files_;
    std::vector<std::string> file_names_ = {"Loading..."};
    models::StorageUsage usage_ = {0, 100 * 1024 * 1024};
    int selected_ = 0;
    bool loading_ = true;
    std::mutex data_mutex_;
    std::thread refresh_thread_;
    std::thread action_thread_;
    ftxui::ScreenInteractive* screen_ = nullptr;
    int active_tab_ = 0; // 0: Browser, 1: Login, 2: Mkdir, 3: Upload, 4: History, 5: Download, 6: Delete, 7: Main Menu, 8: Settings, 9: Themes

    // Multi-selection and collapse sets
    std::set<std::string> selected_paths_;
    std::set<std::string> collapsed_folders_;

    // exit callback helper
    std::function<void()> exit_cb_;

    // Login inputs
    std::string login_url_ = "https://e-aula.ufpel.edu.br";
    std::string login_username_ = "";
    std::string login_password_ = "";
    bool extract_permanent_token_ = true;
    bool save_keyring_ = true;
    std::string login_status_ = "";

    // Mkdir inputs
    std::string mkdir_name_ = "";
    std::string mkdir_status_ = "";

    // Upload inputs
    std::string upload_path_ = "";
    bool upload_recursive_ = false;
    std::string upload_status_ = "";

    // Download inputs
    std::string download_path_ = "";
    std::string download_status_ = "";

    // Delete status
    std::string delete_status_ = "";

    // History inputs
    std::vector<std::string> history_entries_;
    int history_selected_ = 0;

    // Main Menu inputs
    std::vector<std::string> main_menu_entries_ = {"Settings", "Quit"};
    int main_menu_selected_ = 0;

    // Settings inputs
    std::vector<std::string> settings_entries_ = {"Themes", "Clear History", "Clear Data"};
    int settings_selected_ = 0;
    std::string settings_status_ = "";

    // Themes inputs
    std::vector<std::string> theme_names_;
    int theme_selected_ = 0;

    // Components
    ftxui::Component file_menu_;
    ftxui::Component input_url_;
    ftxui::Component input_username_;
    ftxui::Component input_password_;
    ftxui::Component chk_perm_token_;
    ftxui::Component chk_save_keyring_;
    ftxui::Component btn_login_;
    ftxui::Component btn_login_exit_;
    ftxui::Component login_container_;

    ftxui::Component input_mkdir_;
    ftxui::Component btn_mkdir_ok_;
    ftxui::Component btn_mkdir_cancel_;
    ftxui::Component mkdir_container_;

    ftxui::Component input_upload_;
    ftxui::Component chk_upload_recursive_;
    ftxui::Component btn_upload_ok_;
    ftxui::Component btn_upload_cancel_;
    ftxui::Component upload_container_;

    ftxui::Component input_download_;
    ftxui::Component btn_download_ok_;
    ftxui::Component btn_download_cancel_;
    ftxui::Component download_container_;

    ftxui::Component btn_delete_ok_;
    ftxui::Component btn_delete_cancel_;
    ftxui::Component delete_container_;

    ftxui::Component history_menu_;
    ftxui::Component btn_history_close_;
    ftxui::Component history_container_;

    ftxui::Component main_menu_;
    ftxui::Component btn_main_menu_ok_;
    ftxui::Component btn_main_menu_cancel_;
    ftxui::Component main_menu_container_;

    ftxui::Component settings_menu_;
    ftxui::Component btn_settings_ok_;
    ftxui::Component btn_settings_cancel_;
    ftxui::Component settings_container_;

    ftxui::Component theme_menu_;
    ftxui::Component btn_theme_ok_;
    ftxui::Component btn_theme_cancel_;
    ftxui::Component theme_container_;

    ftxui::Component tab_container_;
};

} // namespace mstorage::tui
