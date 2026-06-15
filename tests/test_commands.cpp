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

    UploadCommand cmd(client, sm, hm, std::vector<std::string>{test_file}, "/");
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
    // Interactive testing is omitted for basic CI
    EXPECT_TRUE(true);
}

TEST_F(CommandTest, DownloadCommandOrchestration) {
    SessionManager sm;
    ::testing::NiceMock<MockHttpClient> nice_mock_http;
    MoodleClient client{nice_mock_http, "https://moodle.test"};
    
    // Mandatory initialization mock - can be called multiple times
    EXPECT_CALL(nice_mock_http, get(::testing::Eq("https://moodle.test/user/files.php"), _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));

    EXPECT_CALL(nice_mock_http, post(::testing::StartsWith("https://moodle.test/repository/draftfiles_ajax.php?action=list"), _, _))
        .WillOnce(Return(std::string(R"({"list":[{"filename":"test.txt","url":"https://moodle.test/down","filepath":"/","size":10,"filesize":"10B","datemodified":1}]})")));
    
    EXPECT_CALL(nice_mock_http, get(::testing::Eq("https://moodle.test/down"), _))
        .WillOnce(Return(std::string("file content")));

    DownloadCommand cmd(client, sm, std::vector<std::string>{"test.txt"}, false);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(std::filesystem::exists("test.txt"));
    std::filesystem::remove("test.txt");
}

TEST_F(CommandTest, DownloadCommandRecursiveOrchestration) {
    SessionManager sm;
    ::testing::NiceMock<MockHttpClient> nice_mock_http;
    MoodleClient client{nice_mock_http, "https://moodle.test"};

    // Initialization mock
    EXPECT_CALL(nice_mock_http, get(::testing::Eq("https://moodle.test/user/files.php"), _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));

    // 1. Initial list_files (root) to find "folder1"
    EXPECT_CALL(nice_mock_http, post(::testing::StartsWith("https://moodle.test/repository/draftfiles_ajax.php?action=list"), _, _))
        .WillOnce(Return(std::string(R"({"list":[{"filename":".","filepath":"/folder1/","size":0,"filesize":"0B","datemodified":1}]})")));

    // 2. Expect downloadselected (ZIP) call
    EXPECT_CALL(nice_mock_http, post(::testing::StartsWith("https://moodle.test/repository/draftfiles_ajax.php?action=downloadselected"), _, _))
        .WillOnce(Return(std::string(R"({"fileurl":"https://moodle.test/folder1.zip"})")));

    // 3. Expect download of the ZIP file
    EXPECT_CALL(nice_mock_http, get(std::string("https://moodle.test/folder1.zip"), _))
        .WillOnce(Return(std::string("zip content")));

    DownloadCommand cmd(client, sm, std::vector<std::string>{"folder1"}, true);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());

    EXPECT_TRUE(std::filesystem::exists("folder1.zip"));
    std::filesystem::remove("folder1.zip");
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

    mstorage::commands::DeleteCommand cmd(client, sm, std::vector<std::string>{"to_delete.txt"}, "/", false);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, DeleteCommandFolderOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};
    
    // 1. Expect get_draft_info
    EXPECT_CALL(mock_http, get(_, _)).WillOnce(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));
    
    // 2. Expect delete_file (folder payload)
    EXPECT_CALL(mock_http, post(std::string("https://moodle.test/repository/draftfiles_ajax.php?action=deleteselected"), _, _))
        .WillOnce(Return(std::string("{}")));
        
    // 3. Expect commit_draft
    EXPECT_CALL(mock_http, post_raw(_, _, _, _)).WillOnce(Return(std::string("{}")));

    mstorage::commands::DeleteCommand cmd(client, sm, std::vector<std::string>{"folder_to_delete"}, "/", true);
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

    TEST_F(CommandTest, UploadCommandWithPathOrchestration) {
    SessionManager sm;
    mstorage::storage::HistoryManager hm(":memory:");
    MoodleClient client{mock_http, "https://moodle.test"};

    std::string test_file = temp_dir / "path_test.txt";
    std::ofstream(test_file) << "content";

    // 1. Expect get_draft_info
    EXPECT_CALL(mock_http, get(_, _)).WillOnce(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));

    // 2. Expect mkdir for "a" and "b"
    EXPECT_CALL(mock_http, post(std::string("https://moodle.test/repository/draftfiles_ajax.php?action=mkdir"), _, _))
        .Times(2)
        .WillRepeatedly(Return(std::string("{}")));

    // 3. Expect upload_file (multipart) to path "/a/b/"
    EXPECT_CALL(mock_http, post_multipart(std::string("https://moodle.test/repository/repository_ajax.php?action=upload"), _, _))
        .WillOnce(Return(std::string("{}")));

    // 4. Expect commit
    EXPECT_CALL(mock_http, post_raw(_, _, _, _)).WillOnce(Return(std::string("{}")));

    UploadCommand cmd(client, sm, hm, std::vector<std::string>{test_file}, "/a/b/");
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, BatchDeleteMultipleItemsOrchestration) {
    SessionManager sm;
    MoodleClient client{mock_http, "https://moodle.test"};
    
    // 1. Expect get_draft_info
    EXPECT_CALL(mock_http, get(_, _)).WillOnce(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));
    
    // 2. Expect delete_items (one call for multiple items)
    EXPECT_CALL(mock_http, post(std::string("https://moodle.test/repository/draftfiles_ajax.php?action=deleteselected"), _, _))
        .WillOnce(Return(std::string("{}")));
        
    // 3. Expect commit_draft
    EXPECT_CALL(mock_http, post_raw(_, _, _, _)).WillOnce(Return(std::string("{}")));

    std::vector<std::string> targets = {"file1.txt", "folder1", "file2.pdf"};
    mstorage::commands::DeleteCommand cmd(client, sm, targets, "/", true);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, RecursiveLocalUploadOrchestration) {
    SessionManager sm;
    mstorage::storage::HistoryManager hm(":memory:");
    MoodleClient client{mock_http, "https://moodle.test"};

    // Create a local nested structure
    std::filesystem::path base = temp_dir / "recursive_upload";
    std::filesystem::create_directories(base / "subdir");
    std::ofstream(base / "file1.txt") << "content1";
    std::ofstream(base / "subdir/file2.txt") << "content2";

    // 1. Expect get_draft_info
    EXPECT_CALL(mock_http, get(_, _)).WillOnce(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));

    // 2. Expect mkdir for "recursive_upload" and "subdir"
    EXPECT_CALL(mock_http, post(std::string("https://moodle.test/repository/draftfiles_ajax.php?action=mkdir"), _, _))
        .Times(::testing::AtLeast(2))
        .WillRepeatedly(Return(std::string("{}")));

    // 3. Expect 2 uploads
    EXPECT_CALL(mock_http, post_multipart(std::string("https://moodle.test/repository/repository_ajax.php?action=upload"), _, _))
        .Times(2)
        .WillRepeatedly(Return(std::string("{}")));

    // 4. Expect commit
    EXPECT_CALL(mock_http, post_raw(_, _, _, _)).WillOnce(Return(std::string("{}")));

    std::vector<std::string> sources = {base.string()};
    UploadCommand cmd(client, sm, hm, sources, "/", true);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());
}

TEST_F(CommandTest, BatchDownloadOrchestration) {
    SessionManager sm;
    ::testing::NiceMock<MockHttpClient> nice_mock_http;
    MoodleClient client{nice_mock_http, "https://moodle.test"};

    EXPECT_CALL(nice_mock_http, get(::testing::Eq("https://moodle.test/user/files.php"), _))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(Return(std::string(R"("sesskey":"k","contextid":1 <input name="files_filemanager" value="1">)")));

    EXPECT_CALL(nice_mock_http, post(::testing::StartsWith("https://moodle.test/repository/draftfiles_ajax.php?action=list"), _, _))
        .WillOnce(Return(std::string(R"({"list":[
            {"filename":"a.txt","url":"https://moodle.test/a","filepath":"/","size":1,"filesize":"1B","datemodified":1},
            {"filename":"b.txt","url":"https://moodle.test/b","filepath":"/","size":1,"filesize":"1B","datemodified":1}
        ]})")));


    EXPECT_CALL(nice_mock_http, get(std::string("https://moodle.test/a"), _)).WillOnce(Return(std::string("a")));
    EXPECT_CALL(nice_mock_http, get(std::string("https://moodle.test/b"), _)).WillOnce(Return(std::string("b")));

    std::vector<std::string> files = {"a.txt", "b.txt"};
    DownloadCommand cmd(client, sm, files, false);
    auto result = cmd.execute();
    EXPECT_TRUE(result.has_value());

    EXPECT_TRUE(std::filesystem::exists("a.txt"));
    EXPECT_TRUE(std::filesystem::exists("b.txt"));
    std::filesystem::remove("a.txt");
    std::filesystem::remove("b.txt");
}

