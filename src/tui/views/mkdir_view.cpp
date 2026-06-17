#include "mkdir_view.hpp"
#include <ftxui/dom/elements.hpp>

namespace mstorage::tui::views {

ftxui::Component CreateMkdirView(TuiContext& ctx) {
    auto input_mkdir = ftxui::Input(&ctx.mkdir_name, "Folder Name");
    auto btn_mkdir_ok = ftxui::Button("Create", [&ctx]() {
        if (ctx.perform_mkdir_cb) ctx.perform_mkdir_cb();
    }, ctx.make_button_option(true));
    auto btn_mkdir_cancel = ftxui::Button("Cancel", [&ctx]() {
        ctx.close_dialog();
    }, ctx.make_button_option(false));

    auto mkdir_container = ftxui::Container::Vertical({
        input_mkdir,
        btn_mkdir_ok,
        btn_mkdir_cancel
    });

    return ftxui::Renderer(mkdir_container, [input_mkdir, btn_mkdir_ok, btn_mkdir_cancel, &ctx]() {
        return ftxui::window(ftxui::text(" Create Directory ") | ftxui::bold | ftxui::color(ctx.theme.title),
            ftxui::vbox({
                ftxui::text("Folder Name:"),
                input_mkdir->Render() | ftxui::border,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::hbox({
                    btn_mkdir_ok->Render(),
                    ftxui::text("   "),
                    btn_mkdir_cancel->Render()
                }) | ftxui::center,
                ftxui::text(ctx.mkdir_status) | ftxui::color(ctx.theme.progress_high) | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 40) | ftxui::clear_under | ftxui::center;
    });
}

} // namespace mstorage::tui::views
