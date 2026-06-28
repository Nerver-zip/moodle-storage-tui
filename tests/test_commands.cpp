#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "commands/upload_command.hpp"
#include "commands/list_command.hpp"
#include "commands/login_command.hpp"
#include "commands/download_command.hpp"
#include "commands/history_command.hpp"
#include "commands/delete_command.hpp"
#include "commands/usage_command.hpp"
#include "commands/mkdir_command.hpp"
#include "mock_http_client.hpp"
#include <filesystem>
#include <fstream>

using namespace mstorage::commands;
using namespace mstorage::moodle;
using namespace mstorage::network;
using namespace mstorage::core;
using namespace mstorage::storage;
using ::testing::Return;
using ::testing::_;

class CommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = std::filesystem::temp_directory_path() / "mstorage_cmd_test";
        std::filesystem::create_directories(temp_dir);
        setenv("HOME", temp_dir.c_str(), 1);
        
        SessionManager sm;
        mstorage::models::SessionData data;
        data.moodle_url = "https://moodle.test";
        data.wstoken = "token";
        data.web_cookie = "cookie";
        auto res = sm.save(data);
        (void)res;
    }

    void TearDown() override {
        SessionManager sm;
        sm.clear_credentials();
        std::filesystem::remove_all(temp_dir);
    }

    std::filesystem::path temp_dir;
    ::testing::NiceMock<MockHttpClient> mock_http;
};

// Mocks use NiceMock because cpr::Payload matching in gmock is tedious without custom matchers.
// We verify orchestration via success paths instead.

TEST_F(CommandTest, UploadCommandOrchestration) {
    SessionManager sm;
    HistoryManager hm;
    MoodleClient client{mock_http, "https://moodle.test"};
    client.set_wstoken("token");

    // Mock prepare draft
    EXPECT_CALL(mock_http, post(::testing::Eq("https://moodle.test/webservice/rest/server.php"), _, _))
        .WillRepeatedly(Return(std::string(R"({"draftitemid":123})")));

    // Mock upload multipart
    EXPECT_CALL(mock_http, post_multipart(_, _, _)).WillOnce(Return(std::string("{}")));

    std::string test_file = (temp_dir / "test.txt").string();
    { std::ofstream f(test_file); f << "content"; }

    UploadCommand cmd(client, sm, hm, std::vector<std::string>{test_file}, "/");
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, ListCommandOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};
    client.set_wstoken("token");

    EXPECT_CALL(mock_http, post(::testing::Eq("https://moodle.test/webservice/rest/server.php"), _, _))
        .WillOnce(Return(std::string(R"({"userid":10330,"userpictureurl":"https://m.com/pluginfile.php/23694/user/icon"})")))
        .WillOnce(Return(std::string(R"({"files":[]})")));

    ListCommand cmd(client, sm);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, DownloadCommandOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};
    client.set_wstoken("token");
    client.set_web_cookie("cookie");

    EXPECT_CALL(mock_http, post(::testing::Eq("https://moodle.test/webservice/rest/server.php"), _, _))
        .WillRepeatedly(Return(std::string(R"({"userid":10330,"userpictureurl":"https://m.com/pluginfile.php/23694/user/icon", "files":[{"filename":"test.txt","url":"https://moodle.test/down","filepath":"/","filesize":10,"timemodified":1,"isdir":false}]})")));
    
    // In download, since we check the web cookie for native zip fallback, it might hit user/files.php
    EXPECT_CALL(mock_http, get(::testing::Eq("https://moodle.test/user/files.php"), _))
        .WillRepeatedly(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="123">)")));
    
    EXPECT_CALL(mock_http, get(::testing::StartsWith("https://moodle.test/down"), _))
        .WillOnce(Return(std::string("file content")));

    DownloadCommand cmd(client, sm, std::vector<std::string>{"test.txt"}, false);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, DeleteCommandOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};
    client.set_web_cookie("cookie");

    // Delete flow uses AJAX, so it calls get for draft info, then post for list_files, then post for deleteselected, then post_raw for commit
    EXPECT_CALL(mock_http, get(::testing::Eq("https://moodle.test/user/files.php"), _))
        .WillOnce(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="123">)")));
    
    EXPECT_CALL(mock_http, post(::testing::Eq("https://moodle.test/webservice/rest/server.php"), _, _))
        .WillOnce(Return(std::string(R"({"userid":10330,"userpictureurl":"https://m.com/pluginfile.php/23694/user/icon"})")))
        .WillOnce(Return(std::string(R"({"files":[]})")));
    
    EXPECT_CALL(mock_http, post(::testing::StartsWith("https://moodle.test/repository/draftfiles_ajax.php"), _, _))
        .WillOnce(Return(std::string("[]")));
        
    EXPECT_CALL(mock_http, post_raw(_, _, _, _))
        .WillOnce(Return(std::string("{}")));

    mstorage::commands::DeleteCommand cmd(client, sm, std::vector<std::string>{"to_delete.txt"}, "/");
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, UsageCommandOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};
    client.set_wstoken("token");
    
    EXPECT_CALL(mock_http, post(::testing::Eq("https://moodle.test/webservice/rest/server.php"), _, _))
        .WillOnce(Return(std::string(R"({"filesize": 52428800})")));

    mstorage::commands::StorageUsageCommand cmd(client, sm);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, MkdirCommandOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};
    client.set_web_cookie("cookie");

    EXPECT_CALL(mock_http, post(::testing::Eq("https://moodle.test/webservice/rest/server.php"), _, _))
        .WillOnce(Return(std::string(R"({"userid":10330,"userpictureurl":"https://m.com/pluginfile.php/23694/user/icon"})")));
    EXPECT_CALL(mock_http, get(_, _)).WillOnce(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));
    EXPECT_CALL(mock_http, post(std::string("https://moodle.test/repository/draftfiles_ajax.php?action=mkdir"), _, _))
        .WillOnce(Return(std::string("{}")));
    EXPECT_CALL(mock_http, post_raw(_, _, _, _)).WillOnce(Return(std::string("{}")));

    mstorage::commands::MkdirCommand cmd(client, sm, "new_folder");
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}
