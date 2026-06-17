#include "delete_view.hpp"
#include <ftxui/dom/elements.hpp>
#include <format>

namespace mstorage::tui::views {

ftxui::Component CreateDeleteView(TuiContext& ctx) {
    auto btn_delete_ok = ftxui::Button("Delete", [&ctx]() {
        if (ctx.perform_delete_cb) ctx.perform_delete_cb();
    }, ctx.make_button_option(true));
    auto btn_delete_cancel = ftxui::Button("Cancel", [&ctx]() {
        ctx.close_dialog();
    }, ctx.make_button_option(false));

    auto delete_container = ftxui::Container::Horizontal({
        btn_delete_ok,
        btn_delete_cancel
    });

    return ftxui::Renderer(delete_container, [btn_delete_ok, btn_delete_cancel, &ctx]() {
        std::string target_msg = "";
        if (!ctx.selected_paths.empty()) {
            target_msg = std::format("{} selected items", ctx.selected_paths.size());
        } else if (!ctx.files.empty() && ctx.selected < static_cast<int>(ctx.files.size())) {
            const auto& f = ctx.files[ctx.selected];
            target_msg = f.size_f == "DIR" ? "'" + ctx.get_folder_name(f.filepath) + "'" : "'" + f.filename + "'";
        }

        return ftxui::window(ftxui::text(" Delete Confirmation ") | ftxui::bold | ftxui::color(ctx.theme.title),
            ftxui::vbox({
                ftxui::text("Are you sure you want to delete:") | ftxui::center,
                ftxui::text(""),
                ftxui::text(target_msg) | ftxui::bold | ftxui::color(ctx.theme.progress_high) | ftxui::center,
                ftxui::text(""),
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::hbox({
                    btn_delete_ok->Render(),
                    ftxui::text("   "),
                    btn_delete_cancel->Render()
                }) | ftxui::center,
                ftxui::text(ctx.delete_status) | ftxui::color(ctx.theme.progress_high) | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 50) | ftxui::clear_under | ftxui::center;
    });
}

} // namespace mstorage::tui::views
