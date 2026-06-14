#include <gtest/gtest.h>
#include "core/session_manager.hpp"
#include <filesystem>
#include <fstream>

using namespace mstorage::core;
using namespace mstorage::models;

class SessionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use a temporary directory for tests
        temp_dir = std::filesystem::temp_directory_path() / "mstorage_test";
        std::filesystem::create_directories(temp_dir);
        setenv("HOME", temp_dir.c_str(), 1);
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir);
    }

    std::filesystem::path temp_dir;
};

TEST_F(SessionManagerTest, SaveAndLoadSession) {
    SessionManager manager;
    SessionData data{"https://moodle.com", "secret_key", "cookie_val"};

    auto save_res = manager.save(data);
    ASSERT_TRUE(save_res.has_value());

    auto load_res = manager.load();
    ASSERT_TRUE(load_res.has_value());
    EXPECT_EQ(load_res->moodle_url, data.moodle_url);
    EXPECT_EQ(load_res->sesskey, data.sesskey);
    EXPECT_EQ(load_res->cookie, data.cookie);
}

TEST_F(SessionManagerTest, FilePermissions) {
    SessionManager manager;
    SessionData data{"u", "k", "c"};
    auto res = manager.save(data);
    ASSERT_TRUE(res.has_value());

    auto p = temp_dir / ".config" / "mstorage" / "session.json";
    auto perms = std::filesystem::status(p).permissions();
    
    // Check that group and others have no access
    EXPECT_EQ(perms & std::filesystem::perms::group_all, std::filesystem::perms::none);
    EXPECT_EQ(perms & std::filesystem::perms::others_all, std::filesystem::perms::none);
}
