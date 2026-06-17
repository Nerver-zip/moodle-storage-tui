#include <gtest/gtest.h>
#include "core/session_manager.hpp"
#include <filesystem>
#include <fstream>

using namespace mstorage::core;
using namespace mstorage::models;

class SessionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
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
    SessionData data;
    data.moodle_url = "https://moodle.com";
    data.wstoken = "token_val";
    data.web_cookie = "cookie_val";

    auto save_res = manager.save(data);
    ASSERT_TRUE(save_res.has_value());

    auto load_res = manager.load();
    ASSERT_TRUE(load_res.has_value());
    EXPECT_EQ(load_res->moodle_url, data.moodle_url);
    EXPECT_EQ(load_res->wstoken, data.wstoken);
    EXPECT_EQ(load_res->web_cookie, data.web_cookie);
}

TEST_F(SessionManagerTest, SaveCredentials) {
    SessionManager manager;
    SessionData data;
    data.moodle_url = "https://moodle.com";
    data.wstoken = "t";
    data.web_cookie = "c";
    auto save_res = manager.save(data, "user", "pass");
    ASSERT_TRUE(save_res.has_value());

    auto creds = manager.load_credentials(data.moodle_url);
    ASSERT_TRUE(creds.has_value());
    EXPECT_EQ(creds->username, "user");
    EXPECT_EQ(creds->password, "pass");
}
