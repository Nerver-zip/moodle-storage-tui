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
#include "commands/delete_command.hpp"
#include "commands/history_command.hpp"
#include "commands/usage_command.hpp"
#include "commands/mkdir_command.hpp"
#include "commands/logout_command.hpp"
#include "tui/tui_application.hpp"
#include <iostream>
#include <memory>

namespace mstorage::core {

class Application {
public:
    Application() : app_("Moodle Storage TUI") {
        utils::Logger::init();
        setup_commands();
    }

    void setup_commands() {
        app_.require_subcommand(0, 1);

        // Login command
        auto* login = app_.add_subcommand("login", "Login to Moodle");
        login->add_option("-u,--url", login_url_, "Moodle URL")->required();

        // Logout command
        app_.add_subcommand("logout", "Clear saved credentials and tokens");

        // Upload command
        auto* upload = app_.add_subcommand("upload", "Upload file(s) or folder(s) to Moodle");
        upload->add_option("files", upload_file_paths_, "Paths to the local files or folders")->required();
        upload->add_option("-p,--path", upload_remote_path_, "Remote target path in Moodle")->default_val("/");

        // List command
        app_.add_subcommand("list", "List files in Moodle private area");

        // Download command
        auto* download = app_.add_subcommand("download", "Download file(s) from Moodle");
        download->add_option("filenames", download_filenames_, "Names of the files to download")->required();
        download->add_flag("--no-zip", download_no_zip_, "Do not compress folder server-side, download individual files");

        // Delete command
        auto* del = app_.add_subcommand("delete", "Delete file(s) or folder(s) from Moodle");
        del->add_option("targets", delete_target_names_, "Names of files or folders to delete")->required();
        del->add_option("-p,--path", delete_remote_path_, "Remote path context")->default_val("/");

        // History command
        auto* history = app_.add_subcommand("history", "Show upload history");
        history->add_flag("-c,--clear", history_clear_, "Clear upload history");

        // Usage command
        app_.add_subcommand("usage", "Show Moodle storage usage");

        // Mkdir command
        auto* mkdir = app_.add_subcommand("mkdir", "Create a folder in Moodle");
        mkdir->add_option("foldername", mkdir_foldername_, "Name of the new folder")->required();
    }

    int execute(int argc, char** argv) {
        try {
            app_.parse(argc, argv);
        } catch (const CLI::ParseError& e) {
            return app_.exit(e);
        }

        if (app_.get_subcommands().empty()) {
            mstorage::tui::TuiApplication tui_app(session_manager_, http_client_, history_manager_);
            tui_app.run();
            return 0;
        }

        if (app_.got_subcommand("login")) {
            mstorage::commands::LoginCommand cmd(session_manager_, http_client_, login_url_);
            return handle_result(cmd.execute(), "Login");
        } 

        if (app_.got_subcommand("logout")) {
            mstorage::commands::LogoutCommand cmd(session_manager_);
            return handle_result(cmd.execute(), "Logout");
        }
        
        auto session = session_manager_.load();
        if (!session) return fail("Not logged in. Use 'login' command first.");

        moodle::MoodleClient moodle_client(http_client_, session->moodle_url);
        moodle_client.set_wstoken(session->wstoken);
        moodle_client.set_web_cookie(session->web_cookie);

        if (app_.got_subcommand("upload")) {
            mstorage::commands::UploadCommand cmd(moodle_client, session_manager_, history_manager_, upload_file_paths_, upload_remote_path_);
            return handle_result(cmd.execute(), "Upload");
        }

        if (app_.got_subcommand("list")) {
            mstorage::commands::ListCommand cmd(moodle_client, session_manager_);
            return handle_result(cmd.execute(), "List");
        }

        if (app_.got_subcommand("download")) {
            mstorage::commands::DownloadCommand cmd(moodle_client, session_manager_, download_filenames_, !download_no_zip_);
            return handle_result(cmd.execute(), "Download");
        }

        if (app_.got_subcommand("delete")) {
            mstorage::commands::DeleteCommand cmd(moodle_client, session_manager_, delete_target_names_, delete_remote_path_);
            return handle_result(cmd.execute(), "Delete");
        }

        if (app_.got_subcommand("history")) {
            mstorage::commands::HistoryCommand cmd(history_manager_, history_clear_);
            return handle_result(cmd.execute(), "History");
        }

        if (app_.got_subcommand("usage")) {
            mstorage::commands::StorageUsageCommand cmd(moodle_client, session_manager_);
            return handle_result(cmd.execute(), "Usage");
        }

        if (app_.got_subcommand("mkdir")) {
            mstorage::commands::MkdirCommand cmd(moodle_client, session_manager_, mkdir_foldername_);
            return handle_result(cmd.execute(), "Mkdir");
        }

        return 0;
    }

private:
    int handle_result(const std::expected<void, std::error_code>& res, const std::string& cmd_name) {
        if (!res) {
            std::cerr << cmd_name << " failed: " << res.error().message() << "\n";
            return 1;
        }
        return 0;
    }

    int fail(const std::string& msg) {
        std::cerr << msg << "\n";
        return 1;
    }

    CLI::App app_;
    network::CprClient http_client_;
    core::SessionManager session_manager_;
    storage::HistoryManager history_manager_;

    // Command options
    std::string login_url_;
    std::vector<std::string> upload_file_paths_;
    std::string upload_remote_path_;
    std::vector<std::string> download_filenames_;
    bool download_no_zip_ = false;
    std::vector<std::string> delete_target_names_;
    std::string delete_remote_path_;
    bool history_clear_ = false;
    std::string mkdir_foldername_;
};

} // namespace mstorage::core
