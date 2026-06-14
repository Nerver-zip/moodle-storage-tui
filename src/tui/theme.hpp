#pragma once
#include <ftxui/screen/color.hpp>
#include <string>
#include <map>
#include <fstream>
#include <regex>
#include <filesystem>

namespace mstorage::tui {

struct Theme {
    ftxui::Color main_bg = ftxui::Color::RGB(30, 30, 46);
    ftxui::Color main_fg = ftxui::Color::RGB(205, 214, 244);
    ftxui::Color title = ftxui::Color::RGB(205, 214, 244);
    ftxui::Color hi_fg = ftxui::Color::RGB(137, 180, 250);
    ftxui::Color selected_bg = ftxui::Color::RGB(69, 71, 90);
    ftxui::Color selected_fg = ftxui::Color::RGB(137, 180, 250);
    ftxui::Color inactive_fg = ftxui::Color::RGB(127, 132, 156);
    ftxui::Color box_border = ftxui::Color::RGB(203, 166, 247); // Mauve
    ftxui::Color secondary_box = ftxui::Color::RGB(166, 227, 161); // Green
    ftxui::Color div_line = ftxui::Color::RGB(108, 112, 134);
    ftxui::Color progress_low = ftxui::Color::RGB(166, 227, 161); // Green
    ftxui::Color progress_mid = ftxui::Color::RGB(249, 226, 175); // Yellow
    ftxui::Color progress_high = ftxui::Color::RGB(243, 139, 168); // Red
};

class ThemeManager {
public:
    static Theme load_default() {
        return Theme{};
    }

    static Theme load_from_file(const std::filesystem::path& path) {
        Theme theme;
        if (!std::filesystem::exists(path)) return theme;

        std::ifstream file(path);
        std::string line;
        // Regex to match theme[key]="#hex"
        std::regex pattern(R"regex(theme\[(\w+)\]\s*=\s*"#([0-9a-fA-F]{6})")regex");

        while (std::getline(file, line)) {
            std::smatch match;
            if (std::regex_search(line, match, pattern)) {
                std::string key = match[1];
                std::string hex = match[2];
                auto color = hex_to_color(hex);
                
                apply_color(theme, key, color);
            }
        }
        return theme;
    }

private:
    static ftxui::Color hex_to_color(const std::string& hex) {
        int r = std::stoi(hex.substr(0, 2), nullptr, 16);
        int g = std::stoi(hex.substr(2, 2), nullptr, 16);
        int b = std::stoi(hex.substr(4, 2), nullptr, 16);
        return ftxui::Color::RGB(r, g, b);
    }

    static void apply_color(Theme& theme, const std::string& key, ftxui::Color color) {
        if (key == "main_bg") theme.main_bg = color;
        else if (key == "main_fg") theme.main_fg = color;
        else if (key == "title") theme.title = color;
        else if (key == "hi_fg") theme.hi_fg = color;
        else if (key == "selected_bg") theme.selected_bg = color;
        else if (key == "selected_fg") theme.selected_fg = color;
        else if (key == "inactive_fg") theme.inactive_fg = color;
        else if (key == "cpu_box") theme.box_border = color;
        else if (key == "mem_box") theme.secondary_box = color;
        else if (key == "div_line") theme.div_line = color;
        else if (key == "used_start") theme.progress_low = color;
        else if (key == "available_mid") theme.progress_mid = color;
        else if (key == "available_end") theme.progress_high = color;
    }
};

} // namespace mstorage::tui
