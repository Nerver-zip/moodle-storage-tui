#include "settings_view.hpp"
#include <ftxui/dom/elements.hpp>

namespace mstorage::tui::views {

ftxui::Component CreateSettingsView(TuiContext& ctx, std::function<void()> open_themes_cb) {
    auto settings_menu = ftxui::Menu(&ctx.settings_entries, &ctx.settings_selected, ctx.make_menu_option([&ctx, open_themes_cb]() {
        if (ctx.settings_selected == 0) {
            open_themes_cb();
        } else if (ctx.settings_selected == 1) {
            auto res = ctx.history_manager.clear();
            if (res) {
                ctx.settings_status = "History cleared successfully.";
                ctx.history_entries.clear();
                ctx.history_entries.push_back("<No history found>");
                ctx.history_selected = 0;
            } else {
                ctx.settings_status = "Failed to clear history.";
            }
        } else if (ctx.settings_selected == 2) {
            auto res = ctx.session_manager.clear_credentials();
            if (res) {
                ctx.settings_status = "Credentials and session cleared.";
                ctx.all_files.clear();
                ctx.files.clear();
                ctx.file_names = {"Loading..."};
                ctx.selected = 0;
                ctx.selected_paths.clear();
                ctx.active_tab = 1;
            } else {
                ctx.settings_status = "Failed to clear data.";
            }
        }
    }));

    return ftxui::Renderer(settings_menu, [settings_menu, &ctx]() {
        return ftxui::window(ftxui::text(" Settings ") | ftxui::bold | ftxui::color(ctx.theme.title),
            ftxui::vbox({
                settings_menu->Render() | ftxui::border,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::text(" [Enter] Select  •  [Esc] Back ") | ftxui::dim | ftxui::center,
                ftxui::text(ctx.settings_status) | ftxui::color(ctx.theme.hi_fg) | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 40) | ftxui::clear_under | ftxui::center;
    });
}

} // namespace mstorage::tui::views
