#pragma once
#include "tui/tui_context.hpp"
#include <ftxui/component/component.hpp>

namespace mstorage::tui::views {

ftxui::Component CreateBrowserView(TuiContext& ctx, ftxui::Component file_menu);

} // namespace mstorage::tui::views
