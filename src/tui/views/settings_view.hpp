#pragma once
#include "tui/tui_context.hpp"
#include <ftxui/component/component.hpp>

namespace mstorage::tui::views {

ftxui::Component CreateSettingsView(TuiContext& ctx, std::function<void()> open_themes_cb);

} // namespace mstorage::tui::views
