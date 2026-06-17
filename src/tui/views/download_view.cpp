#include "download_view.hpp"
#include <ftxui/dom/elements.hpp>
#include <format>

namespace mstorage::tui::views {

ftxui::Component CreateDownloadView(TuiContext& ctx) {
    auto input_download = ftxui::Input(&ctx.download_path, "Target local path");
    auto btn_download_ok = ftxui::Button("Download", [&ctx]() {
        if (ctx.perform_download_cb) ctx.perform_download_cb();
    }, ctx.make_button_option(true));
    auto btn_download_cancel = ftxui::Button("Cancel", [&ctx]() {
        ctx.close_dialog();
    }, ctx.make_button_option(false));

    auto download_container = ftxui::Container::Vertical({
        input_download,
        btn_download_ok,
        btn_download_cancel
    });

    return ftxui::Renderer(download_container, [input_download, btn_download_ok, btn_download_cancel, &ctx]() {
        std::string title_txt = ctx.selected_paths.empty() ? " Download File/Folder " : std::format(" Download {} Selected Items ", ctx.selected_paths.size());
        std::string target_msg = "";
        if (!ctx.selected_paths.empty()) {
            target_msg = std::format("{} selected items", ctx.selected_paths.size());
        } else if (!ctx.files.empty() && ctx.selected < static_cast<int>(ctx.files.size())) {
            const auto& f = ctx.files[ctx.selected];
            target_msg = f.size_f == "DIR" ? "'" + ctx.get_folder_name(f.filepath) + "'" : "'" + f.filename + "'";
        }

        return ftxui::window(ftxui::text(title_txt) | ftxui::bold | ftxui::color(ctx.theme.title),
            ftxui::vbox({
                ftxui::text("Downloading " + target_msg + " to:"),
                input_download->Render() | ftxui::border,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::hbox({
                    btn_download_ok->Render(),
                    ftxui::text("   "),
                    btn_download_cancel->Render()
                }) | ftxui::center,
                ftxui::text(ctx.download_status) | ftxui::color(ctx.theme.hi_fg) | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 50) | ftxui::center;
    });
}

} // namespace mstorage::tui::views
