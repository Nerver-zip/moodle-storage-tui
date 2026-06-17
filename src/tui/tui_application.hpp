#pragma once
#include "tui_context.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/screen/screen.hpp>
#include <thread>
#include <memory>
#include <vector>
#include <string>
#include <functional>

class TuiTest;

namespace mstorage::tui {

class TuiApplication {
    friend class ::TuiTest;
public:
    TuiApplication(core::SessionManager& session_manager, network::HttpClient& http_client, storage::HistoryManager& history_manager);
    ~TuiApplication();

    void run();
    ftxui::Component get_root_component(std::function<void()> exit_callback = [](){});

private:
    TuiContext context_;
    std::thread refresh_thread_;
    std::thread action_thread_;
    std::thread spinner_thread_;
    
    // Components
    ftxui::Component tab_container_;

    // Actions
    void trigger_refresh();
    void perform_login();
    void perform_mkdir();
    void perform_upload();
    void perform_download();
    void perform_delete();

    // Helpers
    void open_settings();
    void open_themes();
    void open_history();
    void fetch_recursive_all(moodle::MoodleClient& client, const std::string& cookie, const std::string& path, std::vector<models::MoodleFile>& out_files);
};

} // namespace mstorage::tui
