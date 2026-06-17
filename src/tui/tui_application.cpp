#include "tui_application.hpp"
#include "views/views.hpp"
#include "moodle/shibboleth_auth.hpp"
#include <ftxui/component/screen_interactive.hpp>
#include <spdlog/spdlog.h>

namespace mstorage::tui {

TuiApplication::TuiApplication(core::SessionManager& session_manager, network::HttpClient& http_client, storage::HistoryManager& history_manager)
    : context_(session_manager, http_client, history_manager) {
    
    // Wire context callbacks
    context_.trigger_refresh_cb = [this]() { trigger_refresh(); };
    context_.perform_login_cb = [this]() { perform_login(); };
    context_.perform_mkdir_cb = [this]() { perform_mkdir(); };
    context_.perform_upload_cb = [this]() { perform_upload(); };
    context_.perform_download_cb = [this]() { perform_download(); };
    context_.perform_delete_cb = [this]() { perform_delete(); };

    char* home = getenv("HOME");
    std::filesystem::path config_dir = std::string(home ? home : ".") + "/.config/mstorage/themes";
    context_.ensure_default_themes(config_dir);
    
    std::filesystem::path config_path = config_dir / "default.conf";
    context_.theme = ThemeManager::load_from_file(config_path);
}

TuiApplication::~TuiApplication() {
    if (refresh_thread_.joinable()) {
        refresh_thread_.join();
    }
    if (action_thread_.joinable()) {
        action_thread_.join();
    }
    if (spinner_thread_.joinable()) {
        spinner_thread_.join();
    }
}

void TuiApplication::run() {
    auto screen = ftxui::ScreenInteractive::TerminalOutput();
    context_.screen = &screen;
    
    auto session = context_.session_manager.load();
    if (!session) {
        context_.active_tab = 1; // Login screen
    } else {
        context_.active_tab = 0; // Browser screen
        trigger_refresh();
    }

    auto component = get_root_component(screen.ExitLoopClosure());
    screen.Loop(component);
}

void TuiApplication::trigger_refresh() {
    auto session = context_.session_manager.load();
    if (!session) return;

    if (refresh_thread_.joinable()) {
        refresh_thread_.join();
    }
    if (spinner_thread_.joinable()) {
        spinner_thread_.join();
    }

    context_.loading = true;
    context_.spinner_frame = 0;

    spinner_thread_ = std::thread([this]() {
        while (context_.loading) {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            context_.spinner_frame = (context_.spinner_frame + 1) % 10;
            if (context_.screen) {
                context_.screen->PostEvent(ftxui::Event::Custom);
            }
        }
    });

    refresh_thread_ = std::thread([this, session]() {
        moodle::MoodleClient client(context_.http_client, session->moodle_url);
        client.set_wstoken(session->wstoken);
        client.set_web_cookie(session->web_cookie);
        
        std::vector<models::MoodleFile> all_files;
        
        // Prepend the virtual root directory /
        models::MoodleFile root_dir;
        root_dir.filename = "/";
        root_dir.filepath = "/";
        root_dir.size_f = "DIR";
        root_dir.size = 0;
        root_dir.datemodified = 0;
        all_files.push_back(root_dir);

        fetch_recursive_all(client, session->web_cookie, "/", all_files);
        auto fuse = client.get_usage(session->web_cookie);
        
        {
            std::lock_guard<std::mutex> lock(context_.data_mutex);
            context_.all_files = std::move(all_files);
            if (fuse) context_.usage = *fuse;

            // On first load, collapse all folders by default
            if (context_.first_load) {
                for (const auto& file : context_.all_files) {
                    if (file.size_f == "DIR") {
                        context_.collapsed_folders.insert(file.filepath);
                    }
                }
                context_.first_load = false;
            }

            context_.loading = false;
        }
        if (context_.screen) {
            context_.screen->PostEvent(ftxui::Event::Custom);
        }
    });
}

void TuiApplication::fetch_recursive_all(moodle::MoodleClient& client, const std::string& cookie, const std::string& path, std::vector<models::MoodleFile>& out_files) {
    auto files = client.list_files(cookie, path);
    if (!files) return;

    for (auto& file : *files) {
        if (file.filename == ".") {
            if (file.filepath != "/") {
                models::MoodleFile folder_file = file;
                folder_file.filename = context_.get_folder_name(file.filepath);
                folder_file.size_f = "DIR";
                out_files.push_back(folder_file);

                fetch_recursive_all(client, cookie, file.filepath, out_files);
            }
        } else {
            out_files.push_back(file);
        }
    }
}

void TuiApplication::perform_login() {
    context_.login_status = "Authenticating...";
    if (action_thread_.joinable()) action_thread_.join();
    action_thread_ = std::thread([this]() {
        std::string moodle_url = context_.login_url;
        if (moodle_url.length() > 1 && moodle_url.back() == '/') {
            moodle_url.pop_back();
        }

        moodle::ShibbolethAuth auth(moodle_url);
        auto login_res = auth.login_web(context_.login_username, context_.login_password);
        if (!login_res) {
            context_.login_status = "Login failed: " + login_res.error().message();
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        std::string session_cookie = *login_res;
        std::string wstoken = "";

        if (context_.extract_permanent_token) {
            auto token_res = auth.extract_mobile_token(session_cookie);
            if (!token_res) {
                context_.login_status = "Login failed: " + token_res.error().message();
                context_.screen->PostEvent(ftxui::Event::Custom);
                return;
            }
            wstoken = *token_res;
        }

        models::SessionData data {
            .moodle_url = moodle_url,
            .wstoken = wstoken,
            .web_cookie = session_cookie
        };

        auto save_res = context_.session_manager.save(
            data,
            context_.save_keyring ? context_.login_username : "",
            context_.save_keyring ? context_.login_password : ""
        );

        if (!save_res) {
            context_.login_status = "Failed to save session.";
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        context_.login_status = "";
        context_.login_username = "";
        context_.login_password = "";
        context_.active_tab = 0; // Go to browser screen
        trigger_refresh();
        context_.screen->PostEvent(ftxui::Event::Custom);
    });
}

void TuiApplication::perform_mkdir() {
    if (context_.mkdir_name.empty()) {
        context_.mkdir_status = "Please enter a folder name.";
        return;
    }

    context_.mkdir_status = "Creating folder...";

    if (action_thread_.joinable()) action_thread_.join();
    action_thread_ = std::thread([this]() {
        auto client = context_.get_client();
        if (!client) {
            context_.mkdir_status = "Error: client not initialized.";
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        auto session = context_.session_manager.load();
        auto draft_info = client->get_draft_info(session->web_cookie);
        if (!draft_info) {
            auto creds = context_.session_manager.load_credentials(session->moodle_url);
            if (creds && !creds->username.empty() && !creds->password.empty()) {
                auto refreshed = client->refresh_web_session(creds->username, creds->password);
                if (refreshed) {
                    session->web_cookie = *refreshed;
                    (void)context_.session_manager.save(*session);
                    draft_info = client->get_draft_info(session->web_cookie);
                }
            }
        }

        if (!draft_info) {
            draft_info = client->get_draft_info();
        }

        if (!draft_info) {
            context_.mkdir_status = "Failed to get draft info: " + draft_info.error().message();
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        // Parent path resolution
        std::string parent_path = "/";
        if (!context_.files.empty() && context_.selected < static_cast<int>(context_.files.size())) {
            const auto& file = context_.files[context_.selected];
            parent_path = file.filepath;
        }
        if (parent_path.empty() || parent_path.front() != '/') parent_path = "/" + parent_path;
        if (parent_path.back() != '/') parent_path += "/";

        auto res = client->create_folder(context_.mkdir_name, parent_path, *draft_info, session->web_cookie);
        if (!res) {
            context_.mkdir_status = "Failed to create folder: " + res.error().message();
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        auto commit_res = client->commit_draft(*draft_info, session->web_cookie);
        if (!commit_res) {
            context_.mkdir_status = "Commit failed: " + commit_res.error().message();
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        context_.mkdir_status = "";
        context_.mkdir_name = "";
        context_.active_tab = 0; // Go back to browser
        trigger_refresh();
        context_.screen->PostEvent(ftxui::Event::Custom);
    });
}

void TuiApplication::perform_upload() {
    std::vector<std::filesystem::path> paths_to_upload;
    if (!context_.selected_local_paths.empty()) {
        for (const auto& p : context_.selected_local_paths) {
            paths_to_upload.push_back(p);
        }
    } else {
        if (context_.selected_local_node >= 0 && context_.selected_local_node < static_cast<int>(context_.visible_local_nodes.size())) {
            const auto& node = context_.visible_local_nodes[context_.selected_local_node];
            if (!node.is_parent) {
                paths_to_upload.push_back(node.path);
            }
        }
    }

    if (paths_to_upload.empty()) {
        context_.upload_status = "Please select at least one local file/folder.";
        return;
    }

    context_.upload_status = "Uploading...";

    if (action_thread_.joinable()) action_thread_.join();
    action_thread_ = std::thread([this, paths_to_upload]() {
        auto client = context_.get_client();
        if (!client) {
            context_.upload_status = "Error: client not initialized.";
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        auto session = context_.session_manager.load();
        auto draft_info = client->get_draft_info(session->web_cookie);
        if (!draft_info) {
            context_.upload_status = "Failed to get draft info: " + draft_info.error().message();
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        // Normalize destination path
        std::string parent_path = context_.moodle_upload_path;
        if (parent_path.empty() || parent_path.front() != '/') parent_path = "/" + parent_path;
        if (parent_path.back() != '/') parent_path += "/";

        auto upload_single_file = [&](const std::string& local_path, const std::string& remote_dir) -> std::expected<void, std::error_code> {
            auto res = client->upload_file(local_path, remote_dir, *draft_info, session->web_cookie);
            if (res) {
                (void)context_.history_manager.record_upload(std::filesystem::path(local_path).filename().string(), "TUI");
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

        for (const auto& local_path : paths_to_upload) {
            std::expected<void, std::error_code> res;
            if (std::filesystem::is_directory(local_path)) {
                if (context_.upload_recursive) {
                    res = upload_recursive(local_path, parent_path);
                } else {
                    continue;
                }
            } else {
                res = upload_single_file(local_path.string(), parent_path);
            }

            if (!res) {
                context_.upload_status = "Upload failed for '" + local_path.filename().string() + "': " + res.error().message();
                context_.screen->PostEvent(ftxui::Event::Custom);
                return;
            }
        }

        auto commit_res = client->commit_draft(*draft_info, session->web_cookie);
        if (!commit_res) {
            context_.upload_status = "Commit failed: " + commit_res.error().message();
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        context_.upload_status = "";
        context_.selected_local_paths.clear();
        context_.active_tab = 0; // Go back to browser
        trigger_refresh();
        context_.screen->PostEvent(ftxui::Event::Custom);
    });
}

void TuiApplication::perform_download() {
    if (context_.download_path.empty()) {
        context_.download_status = "Please enter a target path.";
        return;
    }

    context_.download_status = "Downloading...";

    if (action_thread_.joinable()) action_thread_.join();
    action_thread_ = std::thread([this]() {
        auto client = context_.get_client();
        if (!client) {
            context_.download_status = "Error: client not initialized.";
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        auto session = context_.session_manager.load();
        std::vector<models::MoodleFile> items_to_download;
        
        if (!context_.selected_paths.empty()) {
            for (const auto& file : context_.all_files) {
                std::string item_key = file.filepath + "/" + file.filename;
                if (context_.selected_paths.contains(item_key)) {
                    items_to_download.push_back(file);
                }
            }
        } else {
            if (context_.files.empty() || context_.selected >= static_cast<int>(context_.files.size())) {
                context_.download_status = "No file selected.";
                context_.screen->PostEvent(ftxui::Event::Custom);
                return;
            }
            items_to_download.push_back(context_.files[context_.selected]);
        }

        std::expected<void, std::error_code> res;
        for (const auto& file : items_to_download) {
            context_.download_status = "Downloading: " + (file.size_f == "DIR" ? file.filepath : file.filename);
            context_.screen->PostEvent(ftxui::Event::Custom);

            if (file.size_f == "DIR") {
                auto draft_info = client->get_draft_info(session->web_cookie);
                if (draft_info) {
                    auto zip_url = client->zip_folder(file.filepath, *draft_info, session->web_cookie);
                    if (zip_url) {
                        std::string target_file = context_.download_path;
                        if (std::filesystem::is_directory(target_file)) {
                            target_file = (std::filesystem::path(target_file) / (context_.get_folder_name(file.filepath) + ".zip")).string();
                        }
                        res = client->download_file(*zip_url, target_file, session->web_cookie);
                    } else {
                        res = std::unexpected(std::make_error_code(std::errc::not_supported));
                    }
                } else {
                    res = std::unexpected(std::make_error_code(std::errc::permission_denied));
                }
            } else {
                std::string target_file = context_.download_path;
                if (std::filesystem::is_directory(target_file)) {
                    target_file = (std::filesystem::path(target_file) / file.filename).string();
                }
                res = client->download_file(file.url, target_file, session->web_cookie);
            }

            if (!res) {
                context_.download_status = "Download failed for " + file.filename + ": " + res.error().message();
                context_.screen->PostEvent(ftxui::Event::Custom);
                return;
            }
        }

        context_.selected_paths.clear();
        context_.download_status = "";
        context_.active_tab = 0; // Go back to browser
        context_.screen->PostEvent(ftxui::Event::Custom);
    });
}

void TuiApplication::perform_delete() {
    context_.delete_status = "Deleting...";

    if (action_thread_.joinable()) action_thread_.join();
    action_thread_ = std::thread([this]() {
        auto client = context_.get_client();
        if (!client) {
            context_.delete_status = "Error: client not initialized.";
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        auto session = context_.session_manager.load();
        std::string active_cookie = session->web_cookie;
        
        auto draft_info = client->get_draft_info(active_cookie);
        if (!draft_info) {
            auto creds = context_.session_manager.load_credentials(session->moodle_url);
            if (creds && !creds->username.empty() && !creds->password.empty()) {
                auto refreshed = client->refresh_web_session(creds->username, creds->password);
                if (refreshed) {
                    active_cookie = *refreshed;
                    session->web_cookie = active_cookie;
                    (void)context_.session_manager.save(*session);
                    draft_info = client->get_draft_info(active_cookie);
                }
            }
        }

        if (!draft_info) {
            draft_info = client->get_draft_info();
        }

        if (!draft_info) {
            context_.delete_status = "Failed to get draft info: " + draft_info.error().message();
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        std::vector<models::DeleteItem> items;
        if (!context_.selected_paths.empty()) {
            for (const auto& file : context_.all_files) {
                std::string item_key = file.filepath + "/" + file.filename;
                if (context_.selected_paths.contains(item_key)) {
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
            if (context_.files.empty() || context_.selected >= static_cast<int>(context_.files.size())) {
                context_.delete_status = "No file selected.";
                context_.screen->PostEvent(ftxui::Event::Custom);
                return;
            }
            const auto& file = context_.files[context_.selected];
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
            context_.delete_status = "No items to delete.";
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        auto delete_res = client->delete_items(items, *draft_info, active_cookie);
        if (!delete_res) {
            context_.delete_status = "Delete failed: " + delete_res.error().message();
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        auto commit_res = client->commit_draft(*draft_info, active_cookie);
        if (!commit_res) {
            context_.delete_status = "Commit failed: " + commit_res.error().message();
            context_.screen->PostEvent(ftxui::Event::Custom);
            return;
        }

        context_.selected_paths.clear();
        context_.delete_status = "";
        context_.active_tab = 0; // Go back to browser
        trigger_refresh();
        context_.screen->PostEvent(ftxui::Event::Custom);
    });
}

void TuiApplication::open_settings() {
    context_.settings_selected = 0;
    context_.settings_status = "";
    context_.active_tab = 8;
}

void TuiApplication::open_themes() {
    context_.refresh_themes_list();
    context_.active_tab = 9;
}

void TuiApplication::open_history() {
    context_.history_entries.clear();
    auto history_res = context_.history_manager.get_history(100);
    if (history_res) {
        for (const auto& entry : *history_res) {
            context_.history_entries.push_back(std::format("{} -> {} ({})", entry.filename, entry.url, entry.timestamp));
        }
    }
    if (context_.history_entries.empty()) {
        context_.history_entries.push_back("<No history found>");
    }
    context_.history_selected = 0;
    context_.active_tab = 4;
}

ftxui::Component TuiApplication::get_root_component(std::function<void()> exit_callback) {
    context_.exit_cb = exit_callback;

    // Build independent views
    auto login_view = views::CreateLoginView(context_);
    auto mkdir_view = views::CreateMkdirView(context_);
    auto upload_view = views::CreateUploadView(context_);
    auto download_view = views::CreateDownloadView(context_);
    auto delete_view = views::CreateDeleteView(context_);
    auto history_view = views::CreateHistoryView(context_);
    auto main_menu_view = views::CreateMainMenuView(context_, [this]() { open_settings(); });
    auto settings_view = views::CreateSettingsView(context_, [this]() { open_themes(); });
    auto themes_view = views::CreateThemesView(context_);

    // Browser Menu component (used by BrowserView)
    auto file_menu = ftxui::Menu(&context_.file_names, &context_.selected, context_.make_menu_option());
    auto browser_view = views::CreateBrowserView(context_, file_menu);

    tab_container_ = ftxui::Container::Tab({
        browser_view,       // 0: Browser
        login_view,         // 1: Login
        mkdir_view,         // 2: Mkdir
        upload_view,        // 3: Upload
        history_view,       // 4: History
        download_view,      // 5: Download
        delete_view,        // 6: Delete
        main_menu_view,     // 7: Main Menu
        settings_view,      // 8: Settings
        themes_view         // 9: Themes
    }, &context_.active_tab);

    auto renderer = ftxui::Renderer(tab_container_, [this, browser_view]() {
        if (context_.active_tab == 0 || context_.active_tab == 1) {
            return tab_container_->Render();
        } else {
            auto background = browser_view->Render() | ftxui::dim;
            auto overlay = tab_container_->Render();
            return ftxui::dbox({ background, overlay });
        }
    });

    return ftxui::CatchEvent(renderer, [this, exit_callback](ftxui::Event event) {
        if (context_.active_tab == 3) {
            if (event == ftxui::Event::Escape) {
                context_.selected_local_paths.clear();
                context_.close_dialog();
                return true;
            }
            if (event == ftxui::Event::Tab || event == ftxui::Event::TabReverse) {
                if (context_.upload_container) {
                    auto active = context_.upload_container->ActiveChild();
                    size_t count = context_.upload_container->ChildCount();
                    if (count > 0) {
                        size_t current_idx = 0;
                        for (size_t i = 0; i < count; ++i) {
                            if (context_.upload_container->ChildAt(i) == active) {
                                current_idx = i;
                                break;
                            }
                        }
                        size_t next_idx;
                        if (event == ftxui::Event::Tab) {
                            next_idx = (current_idx + 1) % count;
                        } else {
                            next_idx = (current_idx + count - 1) % count;
                        }
                        context_.upload_container->SetActiveChild(context_.upload_container->ChildAt(next_idx));
                        return true;
                    }
                }
            }
            
            if (context_.upload_container && context_.local_files_menu && context_.upload_container->ActiveChild() == context_.local_files_menu) {
                if (event == ftxui::Event::Character(' ')) {
                    if (context_.selected_local_node >= 0 && context_.selected_local_node < static_cast<int>(context_.visible_local_nodes.size())) {
                        const auto& node = context_.visible_local_nodes[context_.selected_local_node];
                        if (!node.is_parent) {
                            if (context_.selected_local_paths.contains(node.path)) {
                                context_.selected_local_paths.erase(node.path);
                            } else {
                                context_.selected_local_paths.insert(node.path);
                            }
                            context_.update_local_nodes();
                        }
                    }
                    return true;
                }
                if (event == ftxui::Event::ArrowRight) {
                    if (context_.selected_local_node >= 0 && context_.selected_local_node < static_cast<int>(context_.visible_local_nodes.size())) {
                        const auto& node = context_.visible_local_nodes[context_.selected_local_node];
                        if (node.is_parent) {
                            context_.go_up_local_dir();
                        } else if (node.is_directory) {
                            context_.expanded_local_dirs.insert(node.path);
                            context_.update_local_nodes();
                        }
                    }
                    return true;
                }
                if (event == ftxui::Event::ArrowLeft) {
                    if (context_.selected_local_node >= 0 && context_.selected_local_node < static_cast<int>(context_.visible_local_nodes.size())) {
                        const auto& node = context_.visible_local_nodes[context_.selected_local_node];
                        if (!node.is_parent && node.is_directory) {
                            context_.expanded_local_dirs.erase(node.path);
                            context_.update_local_nodes();
                        }
                    }
                    return true;
                }
                if (event == ftxui::Event::Return) {
                    if (context_.selected_local_node >= 0 && context_.selected_local_node < static_cast<int>(context_.visible_local_nodes.size())) {
                        const auto& node = context_.visible_local_nodes[context_.selected_local_node];
                        if (node.is_parent) {
                            context_.go_up_local_dir();
                        } else if (node.is_directory) {
                            if (context_.expanded_local_dirs.contains(node.path)) {
                                context_.expanded_local_dirs.erase(node.path);
                            } else {
                                context_.expanded_local_dirs.insert(node.path);
                            }
                            context_.update_local_nodes();
                        }
                    }
                    return true;
                }
            }
        }

        if (context_.active_tab != 0) {
            if (event == ftxui::Event::Escape) {
                if (context_.active_tab == 8) {
                    context_.active_tab = 7;
                } else if (context_.active_tab == 9) {
                    context_.active_tab = 8;
                } else {
                    context_.close_dialog();
                }
                return true;
            }
            return false;
        }

        if (context_.active_tab == 0) {
            if (event == ftxui::Event::Character('q')) {
                if (exit_callback) exit_callback();
                return true;
            }
            if (event == ftxui::Event::Character('r')) {
                trigger_refresh();
                return true;
            }
            if (event == ftxui::Event::Character('u')) {
                context_.upload_status = "";
                context_.active_tab = 3; // Upload Dialog
                
                // Initialize Moodle upload path
                std::string parent_path = "/";
                if (!context_.files.empty() && context_.selected < static_cast<int>(context_.files.size())) {
                    const auto& f = context_.files[context_.selected];
                    parent_path = f.filepath;
                }
                if (parent_path.empty() || parent_path.front() != '/') parent_path = "/" + parent_path;
                if (parent_path.back() != '/') parent_path += "/";
                context_.moodle_upload_path = parent_path;

                // Initialize local browser
                context_.current_local_dir = std::filesystem::current_path();
                context_.expanded_local_dirs.clear();
                context_.selected_local_paths.clear();
                context_.selected_local_node = 0;
                context_.update_local_nodes();

                return true;
            }
            if (event == ftxui::Event::Character('d')) {
                if (!context_.selected_paths.empty() || (!context_.files.empty() && context_.selected < static_cast<int>(context_.files.size()))) {
                    if (context_.selected_paths.empty()) {
                        const auto& file = context_.files[context_.selected];
                        if (file.filepath == "/" && file.filename == "/") {
                            return true;
                        }
                    }
                    context_.delete_status = "";
                    context_.active_tab = 6; // Delete Dialog
                }
                return true;
            }
            if (event == ftxui::Event::Character('h')) {
                open_history();
                return true;
            }
            if (event == ftxui::Event::Character('n')) {
                context_.mkdir_name = "";
                context_.mkdir_status = "";
                context_.active_tab = 2; // Mkdir Dialog
                return true;
            }
            if (event == ftxui::Event::Character('s')) {
                open_settings();
                return true;
            }
            if (event == ftxui::Event::Character('m')) {
                context_.active_tab = 7;
                context_.main_menu_selected = 0;
                return true;
            }
            if (event == ftxui::Event::Character(' ')) {
                if (!context_.files.empty() && context_.selected < static_cast<int>(context_.files.size())) {
                    const auto& file = context_.files[context_.selected];
                    if (file.filepath == "/" && file.filename == "/") {
                        return true;
                    }
                    std::string item_key = file.filepath + "/" + file.filename;
                    if (context_.selected_paths.contains(item_key)) {
                        context_.selected_paths.erase(item_key);
                    } else {
                        context_.selected_paths.insert(item_key);
                    }
                    return true;
                }
            }
            if (event == ftxui::Event::ArrowRight) {
                if (!context_.files.empty() && context_.selected < static_cast<int>(context_.files.size())) {
                    const auto& file = context_.files[context_.selected];
                    if (file.size_f == "DIR") {
                        context_.collapsed_folders.erase(file.filepath);
                        return true;
                    }
                }
            }
            if (event == ftxui::Event::ArrowLeft) {
                if (!context_.files.empty() && context_.selected < static_cast<int>(context_.files.size())) {
                    const auto& file = context_.files[context_.selected];
                    if (file.size_f == "DIR") {
                        context_.collapsed_folders.insert(file.filepath);
                        return true;
                    }
                }
            }
            if (event == ftxui::Event::Return) {
                if (!context_.selected_paths.empty()) {
                    context_.download_path = ".";
                    context_.download_status = "";
                    context_.active_tab = 5; // Download Dialog
                    return true;
                }
                if (!context_.files.empty() && context_.selected < static_cast<int>(context_.files.size())) {
                    const auto& file = context_.files[context_.selected];
                    if (file.filepath == "/" && file.filename == "/") {
                        context_.download_path = "moodle_root.zip";
                    } else if (file.size_f == "DIR") {
                        context_.download_path = context_.get_folder_name(file.filepath) + ".zip";
                    } else {
                        context_.download_path = file.filename;
                    }
                    context_.download_status = "";
                    context_.active_tab = 5; // Download Dialog
                }
                return true;
            }
        }
        return false;
    });
}

} // namespace mstorage::tui
