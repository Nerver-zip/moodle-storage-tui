#pragma once
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include "core/session_manager.hpp"

namespace mstorage::tui {

class TuiApplication {
public:
    TuiApplication(core::SessionManager& session_manager)
        : session_manager_(session_manager) {}

    void run() {
        auto screen = ftxui::ScreenInteractive::TerminalOutput();

        auto session = session_manager_.load();
        std::string status_text = session ? "Logged in to " + session->moodle_url : "Not logged in";

        auto renderer = ftxui::Renderer([&] {
            return ftxui::vbox({
                ftxui::hbox({
                    ftxui::text(" Moodle Storage TUI ") | ftxui::bold | ftxui::border,
                    ftxui::filler(),
                    ftxui::text(status_text) | ftxui::color(ftxui::Color::Green) | ftxui::border,
                }),
                ftxui::separator(),
                ftxui::vbox({
                    ftxui::text("Welcome to Moodle Storage!"),
                    ftxui::text("Use the CLI for now, or wait for more TUI features."),
                    ftxui::filler(),
                    ftxui::text("Press 'q' to quit.") | ftxui::dim,
                }) | ftxui::flex,
            }) | ftxui::border;
        });

        auto component = ftxui::CatchEvent(renderer, [&](ftxui::Event event) {
            if (event == ftxui::Event::Character('q')) {
                screen.ExitLoopClosure()();
                return true;
            }
            return false;
        });

        screen.Loop(component);
    }

private:
    core::SessionManager& session_manager_;
};

} // namespace mstorage::tui
