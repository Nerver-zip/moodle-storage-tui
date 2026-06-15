#pragma once
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include "core/session_manager.hpp"
#include "network/http_client.hpp"
#include "moodle/moodle_client.hpp"
#include "tui/theme.hpp"
#include <vector>
#include <string>
#include <future>
#include <thread>
#include <format>
#include <mutex>

namespace mstorage::tui {

class TuiApplication {
public:
    TuiApplication(core::SessionManager& session_manager, network::HttpClient& http_client)
        : session_manager_(session_manager), http_client_(http_client) {
        char* home = getenv("HOME");
        std::filesystem::path config_path = std::string(home ? home : ".") + "/.config/mstorage/themes/default.conf";
        theme_ = ThemeManager::load_from_file(config_path);
    }

    void run() {
        auto screen = ftxui::ScreenInteractive::TerminalOutput();
        
        auto session = session_manager_.load();
        if (!session) {
            std::cerr << "Not logged in. Use 'login' command first.\n";
            return;
        }

        trigger_refresh();

        auto component = get_root_component(screen.ExitLoopClosure());
        screen.Loop(component);
    }

    ~TuiApplication() {
        if (refresh_thread_.joinable()) {
            refresh_thread_.join();
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
            
            std::vector<models::MoodleFile> all_files;
            std::vector<std::string> display_names;
            
            fetch_recursive(client, session->cookie, "/", 0, all_files, display_names);
            auto fuse = client.get_usage(session->cookie);
            
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                files_ = std::move(all_files);
                file_names_ = std::move(display_names);
                if (fuse) usage_ = *fuse;
                
                if (file_names_.empty()) {
                    file_names_.push_back("<No files found>");
                }
                loading_ = false;
            }
        });
    }

private:
    void fetch_recursive(moodle::MoodleClient& client, const std::string& cookie, const std::string& path, int depth, std::vector<models::MoodleFile>& out_files, std::vector<std::string>& out_names) {
        auto files = client.list_files(cookie, path);
        if (!files) return;

        for (auto& file : *files) {
            std::string indent(depth * 2, ' ');
            
            if (file.filename == ".") {
                if (file.filepath != "/") {
                    std::string display = indent + "📁 " + get_folder_name(file.filepath);
                    out_names.push_back(truncate(display, 40));
                    
                    models::MoodleFile folder_file = file;
                    folder_file.filename = get_folder_name(file.filepath);
                    folder_file.size_f = "DIR";
                    out_files.push_back(folder_file);

                    fetch_recursive(client, cookie, file.filepath, depth + 1, out_files, out_names);
                }
            } else {
                std::string display = indent + "📄 " + file.filename;
                out_names.push_back(truncate(display, 40));
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

public:
    ftxui::Component get_root_component(std::function<void()> exit_callback = [](){}) {
        auto session = session_manager_.load();
        std::string moodle_url = session ? session->moodle_url : "Unknown";

        auto menu = ftxui::Menu(&file_names_, &selected_);
        auto menu_renderer = ftxui::Renderer(menu, [this, menu] {
            return menu->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::color(theme_.main_fg);
        });

        auto renderer = ftxui::Renderer(menu_renderer, [this, menu_renderer, moodle_url] {
            std::string status_text = "Connected: " + moodle_url;
            
            auto header = ftxui::hbox({
                ftxui::text(" 󰊄 Moodle Storage ") | ftxui::bold | ftxui::color(theme_.title),
                ftxui::filler(),
                ftxui::text(" " + status_text + " ") | ftxui::color(theme_.hi_fg),
            }) | ftxui::bgcolor(theme_.selected_bg);

            auto footer = ftxui::hbox({
                ftxui::text(" [q] Quit ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [r] Refresh ") | ftxui::color(theme_.hi_fg),
                ftxui::text(" [u] Upload ") | ftxui::color(theme_.inactive_fg),
                ftxui::text(" [Enter] Download ") | ftxui::color(theme_.inactive_fg),
                ftxui::text(" [d] Delete ") | ftxui::color(theme_.inactive_fg),
                ftxui::filler(),
                ftxui::text(loading_ ? " UPDATING... " : " READY ") | ftxui::bold | ftxui::color(loading_ ? theme_.hi_fg : theme_.secondary_box),
            });

            auto browser_box = ftxui::window(ftxui::text(" Files "), menu_renderer->Render()) 
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

            return ftxui::vbox({
                header,
                ftxui::hbox({
                    browser_box,
                    details_box,
                }) | ftxui::flex,
                footer,
            }) | ftxui::bgcolor(theme_.main_bg);
        });

        return ftxui::CatchEvent(renderer, [this, exit_callback](ftxui::Event event) {
            if (event == ftxui::Event::Character('q')) {
                exit_callback();
                return true;
            }
            if (event == ftxui::Event::Character('r')) {
                trigger_refresh();
                return true;
            }
            return false;
        });
    }

private:
    core::SessionManager& session_manager_;
    network::HttpClient& http_client_;
    Theme theme_;

    // TUI State
    std::vector<models::MoodleFile> files_;
    std::vector<std::string> file_names_ = {"Loading..."};
    models::StorageUsage usage_ = {0, 100 * 1024 * 1024};
    int selected_ = 0;
    bool loading_ = true;
    std::mutex data_mutex_;
    std::thread refresh_thread_;
};

} // namespace mstorage::tui
