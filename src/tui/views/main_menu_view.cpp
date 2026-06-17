#include "main_menu_view.hpp"
#include <ftxui/dom/elements.hpp>

namespace mstorage::tui::views {

ftxui::Component CreateMainMenuView(TuiContext& ctx, std::function<void()> open_settings_cb) {
    auto main_menu = ftxui::Menu(&ctx.main_menu_entries, &ctx.main_menu_selected, ctx.make_menu_option([&ctx, open_settings_cb]() {
        if (ctx.main_menu_selected == 0) {
            open_settings_cb();
        } else if (ctx.main_menu_selected == 1) {
            if (ctx.exit_cb) ctx.exit_cb();
        }
    }));

    return ftxui::Renderer(main_menu, [main_menu, &ctx]() {
        return ftxui::window(ftxui::text(" Main Menu ") | ftxui::bold | ftxui::color(ctx.theme.title),
            ftxui::vbox({
                main_menu->Render() | ftxui::border,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::text(" [Enter] Select  •  [Esc] Back ") | ftxui::dim | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 32) | ftxui::center;
    });
}

} // namespace mstorage::tui::views
