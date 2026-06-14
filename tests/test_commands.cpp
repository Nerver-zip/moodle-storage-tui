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
        
        // Create a dummy session
        SessionManager sm;
        auto res = sm.save({"https://moodle.test", "key", "cookie"});
        (void)res;
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir);
    }

    std::filesystem::path temp_dir;
    MockHttpClient mock_http;
};

TEST_F(CommandTest, UploadCommandOrchestration) {
    SessionManager sm;
    HistoryManager hm;
    MoodleClient client{mock_http, "https://moodle.test"};
    
    // 1. Expect get_draft_info
    std::string html = R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="123">)";
    EXPECT_CALL(mock_http, get(_, _)).WillOnce(Return(html));

    // 2. Expect upload_file (multipart)
    EXPECT_CALL(mock_http, post_multipart(_, _, _)).WillOnce(Return(std::string("{}")));

    // 3. Expect commit_draft (post_raw)
    EXPECT_CALL(mock_http, post_raw(_, _, "application/json", _)).WillOnce(Return(std::string("{}")));

    std::string test_file = (temp_dir / "test.txt").string();
    { std::ofstream f(test_file); f << "content"; }

    UploadCommand cmd(client, sm, hm, test_file);
    auto result = cmd.execute();
    
    EXPECT_TRUE(result.has_value());
    
    // Check if history was recorded
    auto history = hm.get_history();
    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->size(), 1);
}

TEST_F(CommandTest, ListCommandOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};
    
    EXPECT_CALL(mock_http, get(_, _)).WillOnce(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));
    EXPECT_CALL(mock_http, post(_, _, _)).WillOnce(Return(std::string(R"({"list":[]})")));

    ListCommand cmd(client, sm);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, LoginCommandOrchestration) {
    SessionManager sm;
    
    // Expect validation call during login
    EXPECT_CALL(mock_http, get(_, _)).WillOnce(Return(std::string(R"("sesskey":"valid_key","contextid":123 <input name="files_filemanager" value="1">)")));

    LoginCommand cmd(sm, mock_http, "https://moodle.test", "new_cookie");
    auto result = cmd.execute();
    
    EXPECT_TRUE(result.has_value());
    auto session = sm.load();
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->cookie, "new_cookie");
    EXPECT_EQ(session->sesskey, "valid_key");
}

TEST_F(CommandTest, DownloadCommandOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};
    
    // Mock listing to find the file
    EXPECT_CALL(mock_http, get(std::string("https://moodle.test/user/files.php"), _)).WillOnce(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));
    EXPECT_CALL(mock_http, post(std::string("https://moodle.test/repository/draftfiles_ajax.php?action=list"), _, _)).WillOnce(Return(std::string(R"({"list":[{"filename":"test.txt","url":"https://moodle.test/down","filepath":"/","size":10,"filesize":"10B","datemodified":1}]})")));
    
    // Mock actual download
    EXPECT_CALL(mock_http, get(std::string("https://moodle.test/down"), _)).WillOnce(Return(std::string("file content")));

    DownloadCommand cmd(client, sm, "test.txt");
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
    
    EXPECT_TRUE(std::filesystem::exists("test.txt"));
    std::filesystem::remove("test.txt");
}

TEST_F(CommandTest, HistoryCommandOrchestration) {
    HistoryManager hm;
    auto res = hm.record_upload("h1.txt", "u1");
    (void)res;
    
    HistoryCommand cmd(hm);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, DeleteCommandOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};
    
    // 1. Expect get_draft_info
    EXPECT_CALL(mock_http, get(_, _)).WillOnce(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));
    
    // 2. Expect deleteselected
    EXPECT_CALL(mock_http, post(std::string("https://moodle.test/repository/draftfiles_ajax.php?action=deleteselected"), _, _)).WillOnce(Return(std::string("[]")));
    
    // 3. Expect commit_draft
    EXPECT_CALL(mock_http, post_raw(_, _, _, _)).WillOnce(Return(std::string("{}")));

    DeleteCommand cmd(client, sm, "to_delete.txt");
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, UsageCommandOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};
    
    // 1. Expect get_draft_info
    EXPECT_CALL(mock_http, get(_, _)).WillOnce(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));
    
    // 2. Expect list (to get filesize)
    EXPECT_CALL(mock_http, post(std::string("https://moodle.test/repository/draftfiles_ajax.php?action=list"), _, _))
        .WillOnce(Return(std::string(R"({"filesize": 52428800})"))); // 50MB

    mstorage::commands::StorageUsageCommand cmd(client, sm);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
    }

    TEST_F(CommandTest, MkdirCommandOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};

    // 1. Expect get_draft_info
    EXPECT_CALL(mock_http, get(_, _)).WillOnce(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));

    // 2. Expect mkdir
    EXPECT_CALL(mock_http, post(std::string("https://moodle.test/repository/draftfiles_ajax.php?action=mkdir"), _, _))
        .WillOnce(Return(std::string("{}")));

    // 3. Expect commit
    EXPECT_CALL(mock_http, post_raw(_, _, _, _)).WillOnce(Return(std::string("{}")));

    mstorage::commands::MkdirCommand cmd(client, sm, "new_folder");
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
    }
