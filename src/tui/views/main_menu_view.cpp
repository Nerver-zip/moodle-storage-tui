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
    auto btn_main_menu_ok = ftxui::Button("Select", [&ctx, open_settings_cb]() {
        if (ctx.main_menu_selected == 0) {
            open_settings_cb();
        } else if (ctx.main_menu_selected == 1) {
            if (ctx.exit_cb) ctx.exit_cb();
        }
    }, ctx.make_button_option(true));
    auto btn_main_menu_cancel = ftxui::Button("Cancel", [&ctx]() {
        ctx.close_dialog();
    }, ctx.make_button_option(false));

    auto main_menu_container = ftxui::Container::Vertical({
        main_menu,
        btn_main_menu_ok,
        btn_main_menu_cancel
    });

    return ftxui::Renderer(main_menu_container, [main_menu, btn_main_menu_ok, btn_main_menu_cancel, &ctx]() {
        return ftxui::window(ftxui::text(" Main Menu ") | ftxui::bold | ftxui::color(ctx.theme.title),
            ftxui::vbox({
                main_menu->Render() | ftxui::border,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::hbox({
                    btn_main_menu_ok->Render(),
                    ftxui::text("   "),
                    btn_main_menu_cancel->Render()
                }) | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 30) | ftxui::center;
    });
}

} // namespace mstorage::tui::views
