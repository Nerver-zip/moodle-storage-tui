#include "browser_view.hpp"
#include <ftxui/dom/elements.hpp>
#include <format>
#include <ctime>

namespace mstorage::tui::views {

ftxui::Component CreateBrowserView(TuiContext& ctx, ftxui::Component file_menu) {
    return ftxui::Renderer(file_menu, [file_menu, &ctx]() {
        ctx.update_visible_files();

        auto session = ctx.session_manager.load();
        std::string moodle_url = session ? session->moodle_url : "Unknown";

        // Render Browser Screen
        std::string status_text = "Connected: " + moodle_url;
        if (!ctx.selected_paths.empty()) {
            status_text += std::format(" | Selected: {}", ctx.selected_paths.size());
        }
        
        auto title_element = ftxui::hbox({
            ftxui::text(" 󰊄 Moodle Storage ") | ftxui::bold | ftxui::color(ctx.theme.title)
        });

        if (ctx.loading) {
            const std::vector<std::string> spinner_frames = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
            std::string spinner_char = spinner_frames[ctx.spinner_frame % spinner_frames.size()];
            title_element = ftxui::hbox({
                ftxui::text(" 󰊄 Moodle Storage ") | ftxui::bold | ftxui::color(ctx.theme.title),
                ftxui::text("  " + spinner_char + " Refreshing") | ftxui::bold | ftxui::color(ctx.theme.title)
            });
        }

        auto header = ftxui::hbox({
            title_element,
            ftxui::filler(),
            ftxui::text(" " + status_text + " ") | ftxui::color(ctx.theme.hi_fg),
        }) | ftxui::bgcolor(ctx.theme.selected_bg);

        auto footer = ftxui::hbox({
            ftxui::text(" [q] Quit ") | ftxui::color(ctx.theme.hi_fg),
            ftxui::text(" [r] Refresh ") | ftxui::color(ctx.theme.hi_fg),
            ftxui::text(" [u] Upload ") | ftxui::color(ctx.theme.hi_fg),
            ftxui::text(" [Enter] Download ") | ftxui::color(ctx.theme.hi_fg),
            ftxui::text(" [d] Delete ") | ftxui::color(ctx.theme.hi_fg),
            ftxui::text(" [n] Mkdir ") | ftxui::color(ctx.theme.hi_fg),
            ftxui::text(" [h] History ") | ftxui::color(ctx.theme.hi_fg),
            ftxui::text(" [Space] Select ") | ftxui::color(ctx.theme.hi_fg),
            ftxui::text(" [◀/▶] Collapse/Expand ") | ftxui::color(ctx.theme.hi_fg),
            ftxui::text(" [s] Settings ") | ftxui::color(ctx.theme.hi_fg),
            ftxui::text(" [m] Menu ") | ftxui::color(ctx.theme.hi_fg),
            ftxui::filler(),
            ftxui::text(ctx.loading ? " UPDATING... " : " READY ") | ftxui::bold | ftxui::color(ctx.loading ? ctx.theme.hi_fg : ctx.theme.secondary_box),
        });

        auto browser_box = ftxui::window(ftxui::text(" Files "), file_menu->Render()) 
                         | ftxui::color(ctx.theme.box_border) | ftxui::flex;

        double usage_percent = (static_cast<double>(ctx.usage.used_bytes) / ctx.usage.total_bytes);
        auto usage_color = usage_percent > 0.9 ? ctx.theme.progress_high : (usage_percent > 0.7 ? ctx.theme.progress_mid : ctx.theme.progress_low);

        ftxui::Element details_content;
        if (ctx.loading && (ctx.file_names.empty() || ctx.file_names[0] == "Loading...")) {
            details_content = ftxui::center(ftxui::text("Loading data...") | ftxui::color(ctx.theme.hi_fg));
        } else if (ctx.files.empty()) {
            details_content = ftxui::center(ftxui::text("No file selected") | ftxui::dim);
        } else {
            std::lock_guard<std::mutex> lock(ctx.data_mutex);
            if (ctx.selected < static_cast<int>(ctx.files.size())) {
                const auto& f = ctx.files[ctx.selected];
                details_content = ftxui::vbox({
                    ftxui::text("Name: " + f.filename) | ftxui::bold | ftxui::color(ctx.theme.main_fg),
                    ftxui::text("Path: " + f.filepath) | ftxui::color(ctx.theme.inactive_fg),
                    ftxui::separator() | ftxui::color(ctx.theme.div_line),
                    ftxui::text("Size: " + f.size_f) | ftxui::color(ctx.theme.main_fg),
                    ftxui::text("Modified: " + (f.datemodified == 0 ? "-" : std::string(ctime(&f.datemodified)))) | ftxui::color(ctx.theme.inactive_fg),
                    ftxui::filler(),
                    ftxui::text(""),
                    ftxui::vbox({
                        ftxui::text("           √≈≠=××≠               ") | ftxui::color(ctx.theme.hi_fg) | ftxui::center,
                        ftxui::text("      √××××××××√                 ") | ftxui::color(ctx.theme.hi_fg) | ftxui::center,
                        ftxui::text("  √=×××××××××××÷π∞≈≠≠≠≠∞√        ") | ftxui::color(ctx.theme.hi_fg) | ftxui::center,
                        ftxui::text("  π   π×××××××÷≠≠≠≠≈≈≈≠≠≠≠       ") | ftxui::color(ctx.theme.hi_fg) | ftxui::center,
                        ftxui::text("  π    ≠≠==∞π √≠≠≠∞   √≠≠≠∞      ") | ftxui::color(ctx.theme.hi_fg) | ftxui::center,
                        ftxui::text("  ≠    ≠≠≠≈   π≠≠≠∞   π≠≠≠∞      ") | ftxui::color(ctx.theme.hi_fg) | ftxui::center,
                        ftxui::text("  ∞√   ≠≠≠≈   π≠≠≠∞   π≠≠≠∞      ") | ftxui::color(ctx.theme.hi_fg) | ftxui::center,
                        ftxui::text("   π   ≠≠≠≈   π≠≠≈∞   π≠≠≠∞      ") | ftxui::color(ctx.theme.hi_fg) | ftxui::center,
                        ftxui::text("       ≠≠≠≈   π≠≠≠∞   π≠≠≠∞      ") | ftxui::color(ctx.theme.hi_fg) | ftxui::center,
                    }) | ftxui::center,
                    ftxui::text(""),
                    ftxui::filler(),
                    ftxui::text("Storage Usage") | ftxui::color(ctx.theme.title),
                    ftxui::gauge(usage_percent) | ftxui::color(usage_color) | ftxui::borderEmpty,
                    ftxui::text(std::format("{:.2f}MB / 100MB ({:.1f}%)", 
                        ctx.usage.used_bytes / (1024.0 * 1024.0), usage_percent * 100.0)) | ftxui::center | ftxui::color(ctx.theme.main_fg),
                });
            } else {
                details_content = ftxui::center(ftxui::text("Selection out of bounds") | ftxui::color(ctx.theme.progress_high));
            }
        }

        auto details_box = ftxui::window(ftxui::text(" Details "), details_content) 
                         | ftxui::color(ctx.theme.secondary_box) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 45);

        return ftxui::vbox({
            header,
            ftxui::hbox({
                browser_box,
                details_box,
            }) | ftxui::flex,
            footer,
        }) | ftxui::bgcolor(ctx.theme.main_bg);
    });
}

} // namespace mstorage::tui::views
