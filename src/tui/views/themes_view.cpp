#include "themes_view.hpp"
#include <ftxui/dom/elements.hpp>

namespace mstorage::tui::views {

ftxui::Component CreateThemesView(TuiContext& ctx) {
    auto theme_menu = ftxui::Menu(&ctx.theme_names, &ctx.theme_selected, ctx.make_menu_option([&ctx]() {
        ctx.apply_selected_theme();
    }));

    return ftxui::Renderer(theme_menu, [theme_menu, &ctx]() {
        return ftxui::window(ftxui::text(" Themes ") | ftxui::bold | ftxui::color(ctx.theme.title),
            ftxui::vbox({
                theme_menu->Render() | ftxui::border,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::text(" [Enter] Apply  •  [Esc] Back ") | ftxui::dim | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 40) | ftxui::clear_under | ftxui::center;
    });
}

} // namespace mstorage::tui::views
