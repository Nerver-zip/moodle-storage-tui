#pragma once
#include "tui/tui_context.hpp"
#include <ftxui/component/component.hpp>

namespace mstorage::tui::views {

ftxui::Component CreateMainMenuView(TuiContext& ctx, std::function<void()> open_settings_cb);

} // namespace mstorage::tui::views
