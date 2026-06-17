#include "themes_view.hpp"
#include <ftxui/dom/elements.hpp>

namespace mstorage::tui::views {

ftxui::Component CreateThemesView(TuiContext& ctx) {
    auto theme_menu = ftxui::Menu(&ctx.theme_names, &ctx.theme_selected, ctx.make_menu_option([&ctx]() {
        ctx.apply_selected_theme();
    }));
    auto btn_theme_ok = ftxui::Button("Apply", [&ctx]() {
        ctx.apply_selected_theme();
    }, ctx.make_button_option(true));
    auto btn_theme_cancel = ftxui::Button("Back", [&ctx]() {
        ctx.active_tab = 8;
    }, ctx.make_button_option(false));

    auto theme_container = ftxui::Container::Vertical({
        theme_menu,
        btn_theme_ok,
        btn_theme_cancel
    });

    return ftxui::Renderer(theme_container, [theme_menu, btn_theme_ok, btn_theme_cancel, &ctx]() {
        return ftxui::window(ftxui::text(" Themes ") | ftxui::bold | ftxui::color(ctx.theme.title),
            ftxui::vbox({
                theme_menu->Render() | ftxui::border,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::hbox({
                    btn_theme_ok->Render(),
                    ftxui::text("   "),
                    btn_theme_cancel->Render()
                }) | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 40) | ftxui::center;
    });
}

} // namespace mstorage::tui::views
