#include "history_view.hpp"
#include <ftxui/dom/elements.hpp>

namespace mstorage::tui::views {

ftxui::Component CreateHistoryView(TuiContext& ctx) {
    auto history_menu = ftxui::Menu(&ctx.history_entries, &ctx.history_selected, ctx.make_menu_option());
    auto btn_history_close = ftxui::Button("Close", [&ctx]() {
        ctx.close_dialog();
    }, ctx.make_button_option(false));

    auto history_container = ftxui::Container::Vertical({
        history_menu,
        btn_history_close
    });

    return ftxui::Renderer(history_container, [history_menu, btn_history_close, &ctx]() {
        return ftxui::window(ftxui::text(" Upload History ") | ftxui::bold | ftxui::color(ctx.theme.title),
            ftxui::vbox({
                history_menu->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 10) | ftxui::border,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                btn_history_close->Render() | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 70) | ftxui::clear_under | ftxui::center;
    });
}

} // namespace mstorage::tui::views
