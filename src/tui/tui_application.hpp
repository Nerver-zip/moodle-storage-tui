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
        std::filesystem::path config_path = std::string(home ? home : ".") + "/.config/mstorage/themes/default.conf";
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
                    history_manager_.record_upload(std::filesystem::path(local_path).filename().string(), "TUI");
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
                        session_manager_.save(*session);
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
    }

public:
    ftxui::Component get_root_component(std::function<void()> exit_callback = [](){}) {
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
        history_menu_ = ftxui::Menu(&history_entries_, &history_selected_);
        btn_history_close_ = ftxui::Button("Close", [this]() {
            close_dialog();
        }, make_button_option(false));
        history_container_ = ftxui::Container::Vertical({
            history_menu_,
            btn_history_close_
        });

        // --- Browser Components ---
        file_menu_ = ftxui::Menu(&file_names_, &selected_);
        
        // Main Tab Container
        tab_container_ = ftxui::Container::Tab({
            file_menu_,         // 0: Browser
            login_container_,   // 1: Login
            mkdir_container_,   // 2: Mkdir
            upload_container_,  // 3: Upload
            history_container_, // 4: History
            download_container_,// 5: Download
            delete_container_   // 6: Delete
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
                ftxui::text(" [m] Mkdir ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [h] History ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [s] Select ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [◀/▶] Collapse/Expand ") | ftxui::color(theme_.hi_fg),
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
            if (event == ftxui::Event::Character('m')) {
                mkdir_name_ = "";
                mkdir_status_ = "";
                active_tab_ = 2; // Mkdir Dialog
                return true;
            }
            if (event == ftxui::Event::Character('s')) {
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
    int active_tab_ = 0; // 0: Browser, 1: Login, 2: Mkdir, 3: Upload, 4: History, 5: Download, 6: Delete

    // Multi-selection and collapse sets
    std::set<std::string> selected_paths_;
    std::set<std::string> collapsed_folders_;

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

    ftxui::Component tab_container_;
};

} // namespace mstorage::tui
