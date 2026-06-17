#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "tui/tui_application.hpp"
#include "core/session_manager.hpp"
#include "mock_http_client.hpp"
#include "storage/upload_history.hpp"
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/event.hpp>
#include <memory>

using namespace mstorage::tui;
using namespace mstorage::core;
using namespace mstorage::network;
using ::testing::Return;
using ::testing::_;

class TuiTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = std::filesystem::temp_directory_path() / "mstorage_tui_test";
        std::filesystem::create_directories(temp_dir);
        setenv("HOME", temp_dir.c_str(), 1);
        
        // Setup a dummy session
        SessionManager sm;
        mstorage::models::SessionData data;
        data.moodle_url = "https://moodle.test";
        data.wstoken = "token";
        data.web_cookie = "cookie";
        auto res = sm.save(data);
        (void)res;

        hm = std::make_unique<mstorage::storage::HistoryManager>((temp_dir / "uploads.db").string());
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir);
    }

    std::filesystem::path temp_dir;
    MockHttpClient mock_http;
    std::unique_ptr<mstorage::storage::HistoryManager> hm;
};

TEST_F(TuiTest, InitialRenderShowsLoading) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    auto component = app.get_root_component();
    
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, component->Render());
    
    std::string output = screen.ToString();
    
    EXPECT_TRUE(output.find("Moodle Storage") != std::string::npos);
    EXPECT_TRUE(output.find("Loading data...") != std::string::npos);
}

TEST_F(TuiTest, QuitEventHandling) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    bool quit_called = false;
    auto component = app.get_root_component([&]() { quit_called = true; });
    
    component->OnEvent(ftxui::Event::Character('q'));
    
    EXPECT_TRUE(quit_called);
}

TEST_F(TuiTest, RefreshEventHandling) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    auto component = app.get_root_component();
    
    bool handled = component->OnEvent(ftxui::Event::Character('r'));
    EXPECT_TRUE(handled);
}

TEST_F(TuiTest, OpenMkdirDialogOnMKeyPressed) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    auto component = app.get_root_component();
    bool handled = component->OnEvent(ftxui::Event::Character('m'));
    EXPECT_TRUE(handled);
    
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, component->Render());
    std::string output = screen.ToString();
    EXPECT_TRUE(output.find("Create Directory") != std::string::npos);
}

TEST_F(TuiTest, OpenUploadDialogOnUKeyPressed) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    auto component = app.get_root_component();
    bool handled = component->OnEvent(ftxui::Event::Character('u'));
    EXPECT_TRUE(handled);
    
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, component->Render());
    std::string output = screen.ToString();
    EXPECT_TRUE(output.find("Upload File/Folder") != std::string::npos);
}

TEST_F(TuiTest, OpenHistoryDialogOnHKeyPressed) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    auto component = app.get_root_component();
    bool handled = component->OnEvent(ftxui::Event::Character('h'));
    EXPECT_TRUE(handled);
    
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, component->Render());
    std::string output = screen.ToString();
    EXPECT_TRUE(output.find("Upload History") != std::string::npos);
}

TEST_F(TuiTest, ArrowKeysAndSelectionEventHandling) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    auto component = app.get_root_component();
    
    bool s_handled = component->OnEvent(ftxui::Event::Character('s'));
    EXPECT_FALSE(s_handled);

    bool left_handled = component->OnEvent(ftxui::Event::ArrowLeft);
    EXPECT_FALSE(left_handled);

    bool right_handled = component->OnEvent(ftxui::Event::ArrowRight);
    EXPECT_FALSE(right_handled);
}
