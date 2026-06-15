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
#include "commands/mkdir_command.hpp"
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
        
        // Upload command
        auto* upload = app_.add_subcommand("upload", "Upload file(s) or folder(s) to Moodle");
        upload->add_option("files", upload_file_paths_, "Paths to the local files or folders")->required();
        upload->add_flag("-r,--recursive", upload_recursive_, "Upload folder contents recursively");
        upload->add_option("-p,--path", upload_remote_path_, "Target remote path in Moodle")->default_val("/");

        // List command
        app_.add_subcommand("list", "List files in Moodle private area");

        // Download command
        auto* download = app_.add_subcommand("download", "Download file(s) from Moodle");
        download->add_option("filenames", download_filenames_, "Names of the files to download")->required();
        download->add_flag("-r,--recursive", download_recursive_, "Download folders recursively");

        // Delete command
        auto* del = app_.add_subcommand("delete", "Delete file(s) or folder(s) from Moodle");
        del->add_option("names", delete_target_names_, "Names of the files or folders to delete")->required();
        del->add_flag("-r,--recursive", delete_recursive_, "Delete folder recursively");
        del->add_option("-p,--path", delete_remote_path_, "Path where the items are located")->default_val("/");

        // History command
        app_.add_subcommand("history", "Show upload history");

        // Usage command
        app_.add_subcommand("usage", "Show Moodle storage usage");

        // Mkdir command
        auto* mkdir = app_.add_subcommand("mkdir", "Create a folder in Moodle");
        mkdir->add_option("foldername", mkdir_foldername_, "Name of the new folder")->required();
    }

    int execute() {
        if (app_.got_subcommand("login")) {
            mstorage::commands::LoginCommand cmd(session_manager_, http_client_, login_url_);
            return handle_result(cmd.execute(), "Login");
        } 
        
        if (app_.got_subcommand("upload")) {
            auto session = session_manager_.load();
            if (!session) return fail("Not logged in. Use 'login' command first.");

            moodle::MoodleClient moodle_client(http_client_, session->moodle_url);
            mstorage::commands::UploadCommand cmd(moodle_client, session_manager_, history_manager_, upload_file_paths_, upload_remote_path_, upload_recursive_);
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
            mstorage::commands::DownloadCommand cmd(moodle_client, session_manager_, download_filenames_, download_recursive_);
            return handle_result(cmd.execute(), "Download");
        }

        if (app_.got_subcommand("delete")) {
            auto session = session_manager_.load();
            if (!session) return fail("Not logged in.");
            
            moodle::MoodleClient moodle_client(http_client_, session->moodle_url);
            mstorage::commands::DeleteCommand cmd(moodle_client, session_manager_, delete_target_names_, delete_remote_path_, delete_recursive_);
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

        if (app_.got_subcommand("mkdir")) {
            auto session = session_manager_.load();
            if (!session) return fail("Not logged in.");
            
            moodle::MoodleClient moodle_client(http_client_, session->moodle_url);
            mstorage::commands::MkdirCommand cmd(moodle_client, session_manager_, mkdir_foldername_);
            return handle_result(cmd.execute(), "Mkdir");
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
    std::vector<std::string> upload_file_paths_;
    std::string upload_remote_path_;
    bool upload_recursive_ = false;
    std::vector<std::string> download_filenames_;
    bool download_recursive_ = false;
    std::vector<std::string> delete_target_names_;
    std::string delete_remote_path_;
    bool delete_recursive_ = false;
    std::string mkdir_foldername_;
};

} // namespace mstorage::core
