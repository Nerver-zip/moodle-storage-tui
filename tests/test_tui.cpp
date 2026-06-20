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

    auto& get_visible_local_nodes(TuiApplication& app) {
        return app.context_.visible_local_nodes;
    }
    auto& get_selected_local_node(TuiApplication& app) {
        return app.context_.selected_local_node;
    }
    auto& get_upload_container(TuiApplication& app) {
        return app.context_.upload_container;
    }
    auto& get_local_files_menu(TuiApplication& app) {
        return app.context_.local_files_menu;
    }
    auto& get_all_files(TuiApplication& app) {
        return app.context_.all_files;
    }
    auto& get_files(TuiApplication& app) {
        return app.context_.files;
    }
    auto& get_selected(TuiApplication& app) {
        return app.context_.selected;
    }
    auto& get_selected_paths(TuiApplication& app) {
        return app.context_.selected_paths;
    }
    auto& get_active_tab(TuiApplication& app) {
        return app.context_.active_tab;
    }
    auto& get_download_path(TuiApplication& app) {
        return app.context_.download_path;
    }
    auto& get_loading(TuiApplication& app) {
        return app.context_.loading;
    }
    auto& get_context(TuiApplication& app) {
        return app.context_;
    }
    void update_visible_files(TuiApplication& app) {
        app.context_.update_visible_files();
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

TEST_F(TuiTest, OpenMkdirDialogOnNKeyPressed) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    auto component = app.get_root_component();
    bool handled = component->OnEvent(ftxui::Event::Character('n'));
    EXPECT_TRUE(handled);
    
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, component->Render());
    std::string output = screen.ToString();
    EXPECT_TRUE(output.find("Create Directory") != std::string::npos);
}

TEST_F(TuiTest, OpenSettingsDialogOnSKeyPressed) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    auto component = app.get_root_component();
    bool handled = component->OnEvent(ftxui::Event::Character('s'));
    EXPECT_TRUE(handled);
    
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, component->Render());
    std::string output = screen.ToString();
    EXPECT_TRUE(output.find("Settings") != std::string::npos);
}

TEST_F(TuiTest, OpenMainMenuOnMKeyPressed) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    auto component = app.get_root_component();
    bool handled = component->OnEvent(ftxui::Event::Character('m'));
    EXPECT_TRUE(handled);
    
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, component->Render());
    std::string output = screen.ToString();
    EXPECT_TRUE(output.find("Main Menu") != std::string::npos);
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
    
    bool space_handled = component->OnEvent(ftxui::Event::Character(' '));
    EXPECT_FALSE(space_handled);

    bool left_handled = component->OnEvent(ftxui::Event::ArrowLeft);
    EXPECT_FALSE(left_handled);

    bool right_handled = component->OnEvent(ftxui::Event::ArrowRight);
    EXPECT_FALSE(right_handled);
}

TEST_F(TuiTest, ThemeSelectionIntegration) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    auto component = app.get_root_component();
    
    // Press 's' to open Settings
    bool s_pressed = component->OnEvent(ftxui::Event::Character('s'));
    EXPECT_TRUE(s_pressed);
    
    // Press Return on Themes option in Settings to open Themes screen
    bool settings_enter = component->OnEvent(ftxui::Event::Return);
    EXPECT_TRUE(settings_enter);
    
    // Press Return on the first theme in the list to apply it
    bool theme_enter = component->OnEvent(ftxui::Event::Return);
    EXPECT_TRUE(theme_enter);
}

TEST_F(TuiTest, SettingsClearActionsIntegration) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    auto component = app.get_root_component();
    
    // Press 's' to open Settings
    bool s_pressed = component->OnEvent(ftxui::Event::Character('s'));
    EXPECT_TRUE(s_pressed);
    
    // Move selection down to Clear History (index 1)
    bool down_pressed1 = component->OnEvent(ftxui::Event::ArrowDown);
    EXPECT_TRUE(down_pressed1);
    
    // Press Return on Clear History option
    bool history_enter = component->OnEvent(ftxui::Event::Return);
    EXPECT_TRUE(history_enter);
    
    // Move selection down to Clear Data (index 2)
    bool down_pressed2 = component->OnEvent(ftxui::Event::ArrowDown);
    EXPECT_TRUE(down_pressed2);
    
    // Press Return on Clear Data option
    bool data_enter = component->OnEvent(ftxui::Event::Return);
    EXPECT_TRUE(data_enter);
}

TEST_F(TuiTest, UploadLocalFilesBrowserNavigation) {
    auto old_path = std::filesystem::current_path();
    auto test_root = temp_dir / "local_browser_test";
    std::filesystem::create_directories(test_root / "subdir");
    {
        std::ofstream f1(test_root / "file1.txt");
        f1 << "hello";
        std::ofstream f2(test_root / "subdir" / "file2.txt");
        f2 << "world";
    }

    std::filesystem::current_path(test_root);

    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    auto component = app.get_root_component();

    // Open upload dialog
    bool u_pressed = component->OnEvent(ftxui::Event::Character('u'));
    EXPECT_TRUE(u_pressed);

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, component->Render());
    std::string output = screen.ToString();

    // Initially, file1.txt and subdir should be visible, but not file2.txt
    EXPECT_TRUE(output.find("file1.txt") != std::string::npos);
    EXPECT_TRUE(output.find("subdir") != std::string::npos);
    EXPECT_TRUE(output.find("file2.txt") == std::string::npos);
    EXPECT_TRUE(output.find(".. (Go up)") != std::string::npos);

    // 1. Focus the menu (moves focus from input_moodle_path_)
    bool arrow_down_to_menu = component->OnEvent(ftxui::Event::ArrowDown);
    EXPECT_TRUE(arrow_down_to_menu);
    ftxui::Render(screen, component->Render());

    // 2. Focus the container (first ArrowDown)
    bool select_subdir = component->OnEvent(ftxui::Event::ArrowDown);
    EXPECT_TRUE(select_subdir);
    ftxui::Render(screen, component->Render());

    // 3. Move selection down to subdir (third ArrowDown)
    bool arrow_down3 = component->OnEvent(ftxui::Event::ArrowDown);
    EXPECT_TRUE(arrow_down3);
    ftxui::Render(screen, component->Render());

    // 3. Expand subdir (ArrowRight)
    bool expand_subdir = component->OnEvent(ftxui::Event::ArrowRight);
    EXPECT_TRUE(expand_subdir);

    // Render and verify file2.txt is now visible
    ftxui::Render(screen, component->Render());
    output = screen.ToString();
    EXPECT_TRUE(output.find("file2.txt") != std::string::npos);

    // 4. Collapse subdir (ArrowLeft)
    bool collapse_subdir = component->OnEvent(ftxui::Event::ArrowLeft);
    EXPECT_TRUE(collapse_subdir);

    // Render and verify file2.txt is no longer visible
    ftxui::Render(screen, component->Render());
    output = screen.ToString();
    EXPECT_TRUE(output.find("file2.txt") == std::string::npos);

    // 5. Select file1.txt (ArrowDown)
    bool select_file1 = component->OnEvent(ftxui::Event::ArrowDown);
    EXPECT_TRUE(select_file1);
    ftxui::Render(screen, component->Render());

    // 6. Toggle selection on file1.txt (Space)
    bool space_file1 = component->OnEvent(ftxui::Event::Character(' '));
    EXPECT_TRUE(space_file1);

    // Render and verify selection checkbox ☑ is shown
    ftxui::Render(screen, component->Render());
    output = screen.ToString();
    EXPECT_TRUE(output.find("☑") != std::string::npos);

    // 7. Go up directory:
    // Move selection back to index 0:
    // We are at index 2 (file1.txt). Move up twice.
    bool up_to_subdir = component->OnEvent(ftxui::Event::ArrowUp);
    EXPECT_TRUE(up_to_subdir);
    ftxui::Render(screen, component->Render());

    bool up_to_parent = component->OnEvent(ftxui::Event::ArrowUp);
    EXPECT_TRUE(up_to_parent);
    ftxui::Render(screen, component->Render());

    // Press Return on ".. (Go up)"
    bool go_up = component->OnEvent(ftxui::Event::Return);
    EXPECT_TRUE(go_up);

    // Render and verify local_browser_test is visible as a child directory now
    ftxui::Render(screen, component->Render());
    output = screen.ToString();
    EXPECT_TRUE(output.find("local_browser_test") != std::string::npos);

    std::filesystem::current_path(old_path);
}

TEST_F(TuiTest, UploadDialogTabNavigation) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    auto component = app.get_root_component();

    // Open upload dialog
    bool u_pressed = component->OnEvent(ftxui::Event::Character('u'));
    EXPECT_TRUE(u_pressed);

    auto& ctx = get_context(app);
    auto active = ctx.upload_container->ActiveChild();
    // Index 0 should be input_moodle_path
    EXPECT_EQ(ctx.upload_container->ChildAt(0), active);

    // Press Tab
    bool tab_pressed = component->OnEvent(ftxui::Event::Tab);
    EXPECT_TRUE(tab_pressed);
    active = ctx.upload_container->ActiveChild();
    // Index 1 should be local_files_menu
    EXPECT_EQ(ctx.upload_container->ChildAt(1), active);

    // Press Tab again
    tab_pressed = component->OnEvent(ftxui::Event::Tab);
    EXPECT_TRUE(tab_pressed);
    active = ctx.upload_container->ActiveChild();
    // Index 2 should be btn_upload_ok
    EXPECT_EQ(ctx.upload_container->ChildAt(2), active);

    // Press Tab again (loops back to index 0)
    tab_pressed = component->OnEvent(ftxui::Event::Tab);
    EXPECT_TRUE(tab_pressed);
    active = ctx.upload_container->ActiveChild();
    EXPECT_EQ(ctx.upload_container->ChildAt(0), active);

    // Press Shift-Tab (TabReverse) (goes back to index 2)
    bool shift_tab_pressed = component->OnEvent(ftxui::Event::TabReverse);
    EXPECT_TRUE(shift_tab_pressed);
    active = ctx.upload_container->ActiveChild();
    EXPECT_EQ(ctx.upload_container->ChildAt(2), active);
}

TEST_F(TuiTest, VirtualRootFolderTreeAndInteraction) {
    SessionManager sm;
    TuiApplication app(sm, mock_http, *hm);
    
    // Manually add root directory and one file to avoid async http network calls in this test
    mstorage::models::MoodleFile root;
    root.filename = "/";
    root.filepath = "/";
    root.size_f = "DIR";
    root.size = 0;
    root.datemodified = 0;
    
    mstorage::models::MoodleFile file1;
    file1.filename = "test.txt";
    file1.filepath = "/";
    file1.size_f = "10B";
    file1.size = 10;
    file1.datemodified = 123456;
    
    std::vector<mstorage::models::MoodleFile> test_files = {root, file1};
    get_all_files(app) = test_files;
    get_loading(app) = false;
    update_visible_files(app);
    
    auto component = app.get_root_component();
    
    // Check files_ list matches
    EXPECT_EQ(get_files(app).size(), 2);
    EXPECT_EQ(get_files(app)[0].filename, "/");
    EXPECT_EQ(get_files(app)[1].filename, "test.txt");
    
    // 1. Try to select root folder (index 0) with Space
    get_selected(app) = 0;
    bool space_root = component->OnEvent(ftxui::Event::Character(' '));
    EXPECT_TRUE(space_root);
    EXPECT_TRUE(get_selected_paths(app).empty()); // should not select root folder
    
    // 2. Try to select file1 (index 1) with Space
    get_selected(app) = 1;
    bool space_file1 = component->OnEvent(ftxui::Event::Character(' '));
    EXPECT_TRUE(space_file1);
    EXPECT_FALSE(get_selected_paths(app).empty()); // should select file1
    
    // Clear selection
    get_selected_paths(app).clear();
    
    // 3. Try to hit delete ('d') on root folder (index 0)
    get_selected(app) = 0;
    bool del_root = component->OnEvent(ftxui::Event::Character('d'));
    EXPECT_TRUE(del_root);
    EXPECT_NE(get_active_tab(app), 6); // active_tab_ should NOT be 6 (Delete Dialog)
    
    // 4. Try to hit delete ('d') on file1 (index 1)
    get_selected(app) = 1;
    bool del_file1 = component->OnEvent(ftxui::Event::Character('d'));
    EXPECT_TRUE(del_file1);
    EXPECT_EQ(get_active_tab(app), 6); // active_tab_ should be 6
    get_active_tab(app) = 0; // reset
    
    // 5. Try to hit Return on root folder (index 0)
    get_selected(app) = 0;
    bool return_root = component->OnEvent(ftxui::Event::Return);
    EXPECT_TRUE(return_root);
    EXPECT_EQ(get_active_tab(app), 5); // active_tab_ should be 5 (Download Dialog)
    EXPECT_EQ(get_download_path(app), "moodle_root"); // should suggest moodle_root without .zip
}


