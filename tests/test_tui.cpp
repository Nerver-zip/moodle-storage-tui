#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "tui/tui_application.hpp"
#include "core/session_manager.hpp"
#include "mock_http_client.hpp"
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/event.hpp>

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
        auto res = sm.save({"https://moodle.test", "key", "cookie"});
        (void)res;
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir);
    }

    std::filesystem::path temp_dir;
    MockHttpClient mock_http;
};

TEST_F(TuiTest, InitialRenderShowsLoading) {
    SessionManager sm;
    TuiApplication app(sm, mock_http);
    
    auto component = app.get_root_component();
    
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, component->Render());
    
    std::string output = screen.ToString();
    
    EXPECT_TRUE(output.find("Moodle Storage") != std::string::npos);
    EXPECT_TRUE(output.find("Loading data...") != std::string::npos);
}

TEST_F(TuiTest, QuitEventHandling) {
    SessionManager sm;
    TuiApplication app(sm, mock_http);
    
    bool quit_called = false;
    auto component = app.get_root_component([&]() { quit_called = true; });
    
    component->OnEvent(ftxui::Event::Character('q'));
    
    EXPECT_TRUE(quit_called);
}

TEST_F(TuiTest, RefreshEventHandling) {
    SessionManager sm;
    TuiApplication app(sm, mock_http);
    
    auto component = app.get_root_component();
    
    bool handled = component->OnEvent(ftxui::Event::Character('r'));
    EXPECT_TRUE(handled);
}
