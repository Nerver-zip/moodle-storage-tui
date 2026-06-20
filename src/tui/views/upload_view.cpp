#include "upload_view.hpp"
#include <ftxui/dom/elements.hpp>
#include <format>

namespace mstorage::tui::views {

ftxui::Component CreateUploadView(TuiContext& ctx) {
    auto input_moodle_path = ftxui::Input(&ctx.moodle_upload_path, "Moodle upload path");
    auto local_files_menu = ftxui::Menu(&ctx.local_node_names, &ctx.selected_local_node, ctx.make_menu_option());
    
    auto btn_upload_ok = ftxui::Button("Upload", [&ctx]() {
        if (ctx.perform_upload_cb) ctx.perform_upload_cb();
    }, ctx.make_button_option(true));
    
    auto upload_container = ftxui::Container::Vertical({
        input_moodle_path,
        local_files_menu,
        btn_upload_ok
    });

    // Share references to components in ctx for unit testing
    ctx.upload_container = upload_container;
    ctx.local_files_menu = local_files_menu;

    return ftxui::Renderer(upload_container, [input_moodle_path, local_files_menu, btn_upload_ok, &ctx]() {
        auto dialog = ftxui::window(ftxui::text(" Upload File/Folder ") | ftxui::bold | ftxui::color(ctx.theme.title),
            ftxui::vbox({
                ftxui::hbox({
                    ftxui::text("Moodle Destination: ") | ftxui::color(ctx.theme.main_fg) | ftxui::vcenter,
                    input_moodle_path->Render() | ftxui::border | ftxui::flex
                }),
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::text(std::format("Local Files (Current: {})", ctx.current_local_dir.string())) | ftxui::bold | ftxui::color(ctx.theme.title),
                local_files_menu->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 12) | ftxui::border,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                btn_upload_ok->Render() | ftxui::center,
                ftxui::text(ctx.upload_status) | ftxui::color(ctx.theme.hi_fg) | ftxui::center,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::text(" [Tab] Navigate  •  [Space] Select  •  [Enter] Confirm  •  [Esc] Close ") | ftxui::dim | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 75) | ftxui::clear_under | ftxui::center;
        return dialog;
    });
}

} // namespace mstorage::tui::views
