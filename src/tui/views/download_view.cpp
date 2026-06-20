#include "download_view.hpp"
#include <ftxui/dom/elements.hpp>
#include <format>

namespace mstorage::tui::views {

ftxui::Component CreateDownloadView(TuiContext& ctx) {
    auto input_download = ftxui::Input(&ctx.download_path, "Target local path");
    
    auto has_dirs_lambda = [&ctx]() {
        bool has_dirs = false;
        if (!ctx.selected_paths.empty()) {
            for (const auto& file : ctx.all_files) {
                std::string item_key = file.filepath + "/" + file.filename;
                if (ctx.selected_paths.contains(item_key) && file.size_f == "DIR") {
                    has_dirs = true;
                    break;
                }
            }
        } else if (!ctx.files.empty() && ctx.selected < static_cast<int>(ctx.files.size())) {
            if (ctx.files[ctx.selected].size_f == "DIR") {
                has_dirs = true;
            }
        }
        return has_dirs;
    };

    auto chk_zip = ftxui::Checkbox("Compress folder(s) into ZIP on server", &ctx.download_use_zip);
    auto chk_zip_maybe = ftxui::Maybe(chk_zip, has_dirs_lambda);

    ftxui::MenuOption menu_option = ctx.make_menu_option();
    menu_option.on_change = [&ctx]() {
        if (ctx.selected_local_node >= 0 && ctx.selected_local_node < static_cast<int>(ctx.visible_local_nodes.size())) {
            ctx.download_path = ctx.visible_local_nodes[ctx.selected_local_node].path.string();
        }
    };
    auto local_files_menu = ftxui::Menu(&ctx.local_node_names, &ctx.selected_local_node, menu_option);

    auto btn_download_ok = ftxui::Button("Download", [&ctx]() {
        if (ctx.perform_download_cb) ctx.perform_download_cb();
    }, ctx.make_button_option(true));
    
    auto btn_download_cancel = ftxui::Button("Cancel", [&ctx]() {
        ctx.close_dialog();
    }, ctx.make_button_option(false));

    auto download_container = ftxui::Container::Vertical({
        input_download,
        chk_zip_maybe,
        local_files_menu,
        btn_download_ok,
        btn_download_cancel
    });

    ctx.download_container = download_container;
    ctx.download_local_files_menu = local_files_menu;

    return ftxui::Renderer(download_container, [input_download, chk_zip_maybe, local_files_menu, btn_download_ok, btn_download_cancel, &ctx]() {
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
                chk_zip_maybe->Render(),
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::text(std::format("Local Target Selector (Current: {})", ctx.current_local_dir.string())) | ftxui::bold | ftxui::color(ctx.theme.title),
                local_files_menu->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 10) | ftxui::border,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::hbox({
                    btn_download_ok->Render(),
                    ftxui::text("   "),
                    btn_download_cancel->Render()
                }) | ftxui::center,
                ftxui::text(ctx.download_status) | ftxui::color(ctx.theme.hi_fg) | ftxui::center,
                ftxui::separator() | ftxui::color(ctx.theme.div_line),
                ftxui::text(" [Tab] Navigate  •  [Enter] Confirm/Expand  •  [Esc] Close ") | ftxui::dim | ftxui::center
            })
        ) | ftxui::color(ctx.theme.box_border) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 75) | ftxui::clear_under | ftxui::center;
    });
}

} // namespace mstorage::tui::views
