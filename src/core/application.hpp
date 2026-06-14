#pragma once
#include "CLI/CLI.hpp"
#include "utils/logger.hpp"
#include "core/session_manager.hpp"
#include "network/http_client.hpp"
#include "moodle/moodle_client.hpp"
#include "storage/upload_history.hpp"
#include "commands/login_command.hpp"
#include "commands/upload_command.hpp"
#include "commands/list_command.hpp"
#include "commands/download_command.hpp"
#include "commands/history_command.hpp"
#include "commands/delete_command.hpp"
#include "commands/usage_command.hpp"
#include "tui/tui_application.hpp"
#include <memory>

namespace mstorage::core {

class Application {
public:
    Application() : app_("Moodle Storage TUI") {
        setup_cli();
    }

    int run(int argc, char** argv) {
        utils::Logger::init();
        
        try {
            app_.parse(argc, argv);
        } catch (const CLI::ParseError& e) {
            return app_.exit(e);
        }

        return execute();
    }

private:
    void setup_cli() {
        app_.require_subcommand(0, 1);

        // Login command
        auto* login = app_.add_subcommand("login", "Login to Moodle");
        login->add_option("-u,--url", login_url_, "Moodle URL")->required();
        login->add_option("-c,--cookie", login_cookie_, "MoodleSession cookie")->required();
        
        // Upload command
        auto* upload = app_.add_subcommand("upload", "Upload a file to Moodle");
        upload->add_option("file", upload_file_path_, "Path to the file")->required();

        // List command
        app_.add_subcommand("list", "List files in Moodle private area");

        // Download command
        auto* download = app_.add_subcommand("download", "Download a file from Moodle");
        download->add_option("filename", download_filename_, "Name of the file to download")->required();

        // Delete command
        auto* del = app_.add_subcommand("delete", "Delete a file from Moodle");
        del->add_option("filename", delete_filename_, "Name of the file to delete")->required();

        // History command
        app_.add_subcommand("history", "Show upload history");

        // Usage command
        app_.add_subcommand("usage", "Show Moodle storage usage");
    }

    int execute() {
        if (app_.got_subcommand("login")) {
            mstorage::commands::LoginCommand cmd(session_manager_, http_client_, login_url_, login_cookie_);
            return handle_result(cmd.execute(), "Login");
        } 
        
        if (app_.got_subcommand("upload")) {
            auto session = session_manager_.load();
            if (!session) return fail("Not logged in. Use 'login' command first.");
            
            moodle::MoodleClient moodle_client(http_client_, session->moodle_url);
            mstorage::commands::UploadCommand cmd(moodle_client, session_manager_, history_manager_, upload_file_path_);
            return handle_result(cmd.execute(), "Upload");
        }

        if (app_.got_subcommand("list")) {
            auto session = session_manager_.load();
            if (!session) return fail("Not logged in.");
            
            moodle::MoodleClient moodle_client(http_client_, session->moodle_url);
            mstorage::commands::ListCommand cmd(moodle_client, session_manager_);
            return handle_result(cmd.execute(), "List");
        }

        if (app_.got_subcommand("download")) {
            auto session = session_manager_.load();
            if (!session) return fail("Not logged in.");
            
            moodle::MoodleClient moodle_client(http_client_, session->moodle_url);
            mstorage::commands::DownloadCommand cmd(moodle_client, session_manager_, download_filename_);
            return handle_result(cmd.execute(), "Download");
        }

        if (app_.got_subcommand("delete")) {
            auto session = session_manager_.load();
            if (!session) return fail("Not logged in.");
            
            moodle::MoodleClient moodle_client(http_client_, session->moodle_url);
            mstorage::commands::DeleteCommand cmd(moodle_client, session_manager_, delete_filename_);
            return handle_result(cmd.execute(), "Delete");
        }

        if (app_.got_subcommand("history")) {
            mstorage::commands::HistoryCommand cmd(history_manager_);
            return handle_result(cmd.execute(), "History");
        }

        if (app_.got_subcommand("usage")) {
            auto session = session_manager_.load();
            if (!session) return fail("Not logged in.");
            
            moodle::MoodleClient moodle_client(http_client_, session->moodle_url);
            mstorage::commands::StorageUsageCommand cmd(moodle_client, session_manager_);
            return handle_result(cmd.execute(), "Usage");
        }

        // TUI Mode (Default)
        tui::TuiApplication tui_app(session_manager_, http_client_);
        tui_app.run();
        return 0;
    }

    int handle_result(std::expected<void, std::error_code> result, std::string_view op) {
        if (!result) {
            std::cerr << op << " failed: " << result.error().message() << "\n";
            return 1;
        }
        return 0;
    }

    int fail(std::string_view msg) {
        std::cerr << msg << "\n";
        return 1;
    }

    CLI::App app_;
    SessionManager session_manager_;
    network::CprClient http_client_;
    storage::HistoryManager history_manager_;

    // CLI Options
    std::string login_url_, login_cookie_;
    std::string upload_file_path_;
    std::string download_filename_;
    std::string delete_filename_;
};

} // namespace mstorage::core
