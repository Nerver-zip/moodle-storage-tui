#include "tui_context.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace mstorage::tui {

TuiContext::TuiContext(core::SessionManager& sm, network::HttpClient& hc, storage::HistoryManager& hm)
    : session_manager(sm), http_client(hc), history_manager(hm) {}

void TuiContext::trigger_refresh() {
    if (trigger_refresh_cb) {
        trigger_refresh_cb();
    }
}

void TuiContext::close_dialog() {
    active_tab = 0;
    mkdir_name = "";
    mkdir_status = "";
    upload_path = "";
    upload_status = "";
    download_path = "";
    download_status = "";
    delete_status = "";
    settings_status = "";
}

std::unique_ptr<moodle::MoodleClient> TuiContext::get_client() {
    auto session = session_manager.load();
    if (!session) return nullptr;
    auto client = std::make_unique<moodle::MoodleClient>(http_client, session->moodle_url);
    client->set_wstoken(session->wstoken);
    client->set_web_cookie(session->web_cookie);
    return client;
}

void TuiContext::add_local_directory_contents(const std::filesystem::path& dir, int depth) {
    std::error_code err;
    if (!std::filesystem::exists(dir, err)) return;
    
    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(dir, err)) {
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        std::error_code e1, e2;
        bool a_dir = a.is_directory(e1);
        bool b_dir = b.is_directory(e2);
        if (a_dir != b_dir) {
            return a_dir > b_dir;
        }
        return a.path().filename() < b.path().filename();
    });

    for (const auto& entry : entries) {
        std::error_code e;
        LocalFileNode node;
        node.path = entry.path();
        node.name = entry.path().filename().string();
        node.is_directory = entry.is_directory(e);
        node.is_parent = false;

        visible_local_nodes.push_back(node);

        std::string indent(depth * 4, ' ');
        std::string select_indicator = selected_local_paths.contains(node.path) ? "☑ " : "☐ ";
        
        std::string prefix = select_indicator;
        if (node.is_directory) {
            bool is_expanded = expanded_local_dirs.contains(node.path);
            prefix += (is_expanded ? "📁 ▼ " : "📁 ▶ ");
            local_node_names.push_back(indent + prefix + node.name);
            
            if (is_expanded) {
                add_local_directory_contents(node.path, depth + 1);
            }
        } else {
            prefix += "📄 ";
            local_node_names.push_back(indent + prefix + node.name);
        }
    }
}

void TuiContext::update_local_nodes() {
    visible_local_nodes.clear();
    local_node_names.clear();

    std::error_code ec;
    if (current_local_dir.has_parent_path() && current_local_dir != current_local_dir.root_path()) {
        LocalFileNode parent_node;
        parent_node.path = current_local_dir.parent_path();
        parent_node.name = ".. (Go up)";
        parent_node.is_directory = true;
        parent_node.is_parent = true;
        
        visible_local_nodes.push_back(parent_node);
        local_node_names.push_back("  📁 " + parent_node.name);
    }

    add_local_directory_contents(current_local_dir, 0);

    if (local_node_names.empty()) {
        local_node_names.push_back("<Empty Directory>");
    }
    
    if (selected_local_node >= static_cast<int>(visible_local_nodes.size())) {
        selected_local_node = static_cast<int>(visible_local_nodes.size()) - 1;
    }
    if (selected_local_node < 0) {
        selected_local_node = 0;
    }
}

void TuiContext::go_up_local_dir() {
    std::error_code ec;
    if (current_local_dir.has_parent_path()) {
        auto parent = current_local_dir.parent_path();
        if (std::filesystem::exists(parent, ec)) {
            current_local_dir = parent;
            expanded_local_dirs.clear();
            selected_local_node = 0;
            update_local_nodes();
        }
    }
}

bool TuiContext::is_visible(const models::MoodleFile& file) {
    for (const auto& collapsed : collapsed_folders) {
        if (file.filepath.starts_with(collapsed) && file.filepath.length() > collapsed.length()) {
            return false;
        }
        if (file.filepath == collapsed && file.filename != get_folder_name(collapsed)) {
            return false;
        }
    }
    return true;
}

void TuiContext::update_visible_files() {
    files.clear();
    file_names.clear();

    if (all_files.empty()) {
        if (loading) {
            file_names = {"Loading..."};
        } else {
            file_names = {"<No files found>"};
        }
        selected = 0;
        return;
    }

    auto term_size = ftxui::Terminal::Size();
    int available_width = term_size.dimx - 45 - 12;
    if (available_width < 15) available_width = 15;

    for (const auto& file : all_files) {
        if (!is_visible(file)) continue;

        files.push_back(file);

        int depth = std::count(file.filepath.begin(), file.filepath.end(), '/');
        if (file.filepath.back() == '/') {
            depth--;
        }
        if (file.size_f == "DIR") {
            depth--;
        }
        if (depth < 0) depth = 0;

        if (!(file.filepath == "/" && file.filename == "/")) {
            depth++;
        }

        std::string indent(depth * 4, ' ');
        
        std::string select_indicator = "☐ ";
        std::string item_key = file.filepath + "/" + file.filename;
        if (selected_paths.contains(item_key)) {
            select_indicator = "☑ ";
        }

        std::string prefix = select_indicator;
        if (file.size_f == "DIR") {
            bool is_collapsed = collapsed_folders.contains(file.filepath);
            prefix += (is_collapsed ? "📁 ▶ " : "📁 ▼ ");
            std::string display = indent + prefix + file.filename;
            file_names.push_back(truncate(display, available_width));
        } else {
            prefix += "📄 ";
            std::string display = indent + prefix + file.filename;
            file_names.push_back(truncate(display, available_width));
        }
    }

    if (file_names.empty()) {
        file_names.push_back("<No files found>");
        selected = 0;
    } else if (selected >= static_cast<int>(files.size())) {
        selected = static_cast<int>(files.size()) - 1;
    }
}

std::string TuiContext::truncate(std::string str, size_t width) {
    if (str.length() > width) {
        return str.substr(0, width - 3) + "...";
    }
    return str;
}

std::string TuiContext::get_folder_name(const std::string& filepath) {
    if (filepath == "/") return "/";
    std::string clean_path = filepath;
    if (clean_path.back() == '/') clean_path.pop_back();
    auto pos = clean_path.find_last_of('/');
    if (pos != std::string::npos) {
        return clean_path.substr(pos + 1);
    }
    return clean_path;
}

void TuiContext::ensure_default_themes(const std::filesystem::path& theme_dir) {
    std::filesystem::create_directories(theme_dir);
    
    struct DefaultTheme {
        std::string name;
        std::string content;
    };
    
    std::vector<DefaultTheme> defaults = {
        {"catppuccin_mocha.conf", 
         "# Catppuccin Mocha\n"
         "theme[main_bg]=\"#1e1e2e\"\ntheme[main_fg]=\"#cdd6f4\"\ntheme[title]=\"#cdd6f4\"\ntheme[hi_fg]=\"#89b4fa\"\n"
         "theme[selected_bg]=\"#45475a\"\ntheme[selected_fg]=\"#89b4fa\"\ntheme[inactive_fg]=\"#7f849c\"\n"
         "theme[cpu_box]=\"#cba6f7\"\ntheme[mem_box]=\"#a6e3a1\"\ntheme[div_line]=\"#6c7086\"\n"
         "theme[used_start]=\"#a6e3a1\"\ntheme[available_mid]=\"#f9e2af\"\ntheme[available_end]=\"#f38ba8\"\n"},
         
        {"catppuccin_macchiato.conf",
         "# Catppuccin Macchiato\n"
         "theme[main_bg]=\"#24273a\"\ntheme[main_fg]=\"#cad3f5\"\ntheme[title]=\"#cad3f5\"\ntheme[hi_fg]=\"#8aadf4\"\n"
         "theme[selected_bg]=\"#363a4f\"\ntheme[selected_fg]=\"#8aadf4\"\ntheme[inactive_fg]=\"#8087a2\"\n"
         "theme[cpu_box]=\"#c6a0f6\"\ntheme[mem_box]=\"#a6da95\"\ntheme[div_line]=\"#6e738d\"\n"
         "theme[used_start]=\"#a6da95\"\ntheme[available_mid]=\"#eed49f\"\ntheme[available_end]=\"#ed8796\"\n"},
         
        {"catppuccin_frappe.conf",
         "# Catppuccin Frappe\n"
         "theme[main_bg]=\"#303446\"\ntheme[main_fg]=\"#c6d0f5\"\ntheme[title]=\"#c6d0f5\"\ntheme[hi_fg]=\"#8caaee\"\n"
         "theme[selected_bg]=\"#414559\"\ntheme[selected_fg]=\"#8caaee\"\ntheme[inactive_fg]=\"#838ba7\"\n"
         "theme[cpu_box]=\"#ca9ee6\"\ntheme[mem_box]=\"#a6d189\"\ntheme[div_line]=\"#737994\"\n"
         "theme[used_start]=\"#a6d189\"\ntheme[available_mid]=\"#e5c890\"\ntheme[available_end]=\"#e78284\"\n"},
         
        {"catppuccin_latte.conf",
         "# Catppuccin Latte\n"
         "theme[main_bg]=\"#eff1f5\"\ntheme[main_fg]=\"#4c4f69\"\ntheme[title]=\"#4c4f69\"\ntheme[hi_fg]=\"#1e66f5\"\n"
         "theme[selected_bg]=\"#ccd0da\"\ntheme[selected_fg]=\"#1e66f5\"\ntheme[inactive_fg]=\"#7c7f93\"\n"
         "theme[cpu_box]=\"#8839ef\"\ntheme[mem_box]=\"#40a02b\"\ntheme[div_line]=\"#9ca0b0\"\n"
         "theme[used_start]=\"#40a02b\"\ntheme[available_mid]=\"#df8e1d\"\ntheme[available_end]=\"#d20f39\"\n"},
         
        {"dracula.conf",
         "# Dracula Theme\n"
         "theme[main_bg]=\"#282a36\"\ntheme[main_fg]=\"#f8f8f2\"\ntheme[title]=\"#f8f8f2\"\ntheme[hi_fg]=\"#8be9fd\"\n"
         "theme[selected_bg]=\"#44475a\"\ntheme[selected_fg]=\"#50fa7b\"\ntheme[inactive_fg]=\"#6272a4\"\n"
         "theme[cpu_box]=\"#bd93f9\"\ntheme[mem_box]=\"#50fa7b\"\ntheme[div_line]=\"#44475a\"\n"
         "theme[used_start]=\"#50fa7b\"\ntheme[available_mid]=\"#ffb86c\"\ntheme[available_end]=\"#ff5555\"\n"},
         
        {"tokyo_night.conf",
         "# Tokyo Night (Storm)\n"
         "theme[main_bg]=\"#24283b\"\ntheme[main_fg]=\"#a9b1d6\"\ntheme[title]=\"#a9b1d6\"\ntheme[hi_fg]=\"#7aa2f7\"\n"
         "theme[selected_bg]=\"#2f3549\"\ntheme[selected_fg]=\"#7aa2f7\"\ntheme[inactive_fg]=\"#565f89\"\n"
         "theme[cpu_box]=\"#bb9af7\"\ntheme[mem_box]=\"#9ece6a\"\ntheme[div_line]=\"#3b4261\"\n"
         "theme[used_start]=\"#9ece6a\"\ntheme[available_mid]=\"#e0af68\"\ntheme[available_end]=\"#f7768e\"\n"},
         
        {"gruvbox_dark.conf",
         "# Gruvbox Dark\n"
         "theme[main_bg]=\"#282828\"\ntheme[main_fg]=\"#ebdbb2\"\ntheme[title]=\"#ebdbb2\"\ntheme[hi_fg]=\"#458588\"\n"
         "theme[selected_bg]=\"#3c3836\"\ntheme[selected_fg]=\"#fabd2f\"\ntheme[inactive_fg]=\"#928374\"\n"
         "theme[cpu_box]=\"#b16286\"\ntheme[mem_box]=\"#b8bb26\"\ntheme[div_line]=\"#504945\"\n"
         "theme[used_start]=\"#b8bb26\"\ntheme[available_mid]=\"#fabd2f\"\ntheme[available_end]=\"#fb4934\"\n"},
         
        {"gruvbox_light.conf",
         "# Gruvbox Light\n"
         "theme[main_bg]=\"#fbf1c7\"\ntheme[main_fg]=\"#3c3836\"\ntheme[title]=\"#3c3836\"\ntheme[hi_fg]=\"#076678\"\n"
         "theme[selected_bg]=\"#ebdbb2\"\ntheme[selected_fg]=\"#b57614\"\ntheme[inactive_fg]=\"#7c6f64\"\n"
         "theme[cpu_box]=\"#8f3f71\"\ntheme[mem_box]=\"#79740e\"\ntheme[div_line]=\"#d5c4a1\"\n"
         "theme[used_start]=\"#79740e\"\ntheme[available_mid]=\"#b57614\"\ntheme[available_end]=\"#9d0006\"\n"},
         
        {"nord.conf",
         "# Nord Theme\n"
         "theme[main_bg]=\"#2e3440\"\ntheme[main_fg]=\"#d8dee9\"\ntheme[title]=\"#e5e9f0\"\ntheme[hi_fg]=\"#88c0d0\"\n"
         "theme[selected_bg]=\"#3b4252\"\ntheme[selected_fg]=\"#88c0d0\"\ntheme[inactive_fg]=\"#4c566a\"\n"
         "theme[cpu_box]=\"#b48ead\"\ntheme[mem_box]=\"#a3be8c\"\ntheme[div_line]=\"#434c5e\"\n"
         "theme[used_start]=\"#a3be8c\"\ntheme[available_mid]=\"#ebcb8b\"\ntheme[available_end]=\"#bf616a\"\n"},
         
        {"one_dark.conf",
         "# One Dark\n"
         "theme[main_bg]=\"#282c34\"\ntheme[main_fg]=\"#abb2bf\"\ntheme[title]=\"#abb2bf\"\ntheme[hi_fg]=\"#61afef\"\n"
         "theme[selected_bg]=\"#3e4452\"\ntheme[selected_fg]=\"#61afef\"\ntheme[inactive_fg]=\"#5c6370\"\n"
         "theme[cpu_box]=\"#c678dd\"\ntheme[mem_box]=\"#98c379\"\ntheme[div_line]=\"#4b5263\"\n"
         "theme[used_start]=\"#98c379\"\ntheme[available_mid]=\"#e5c07b\"\ntheme[available_end]=\"#e06c75\"\n"},
         
        {"solarized_dark.conf",
         "# Solarized Dark\n"
         "theme[main_bg]=\"#002b36\"\ntheme[main_fg]=\"#839496\"\ntheme[title]=\"#93a1a1\"\ntheme[hi_fg]=\"#268bd2\"\n"
         "theme[selected_bg]=\"#073642\"\ntheme[selected_fg]=\"#b58900\"\ntheme[inactive_fg]=\"#586e75\"\n"
         "theme[cpu_box]=\"#d33682\"\ntheme[mem_box]=\"#859900\"\ntheme[div_line]=\"#073642\"\n"
         "theme[used_start]=\"#859900\"\ntheme[available_mid]=\"#b58900\"\ntheme[available_end]=\"#dc322f\"\n"},
         
        {"everforest_dark.conf",
         "# Everforest Dark\n"
         "theme[main_bg]=\"#2b3339\"\ntheme[main_fg]=\"#d3c6aa\"\ntheme[title]=\"#d3c6aa\"\ntheme[hi_fg]=\"#7fbbb3\"\n"
         "theme[selected_bg]=\"#323c41\"\ntheme[selected_fg]=\"#dbbc7f\"\ntheme[inactive_fg]=\"#859289\"\n"
         "theme[cpu_box]=\"#d699b6\"\ntheme[mem_box]=\"#a7c080\"\ntheme[div_line]=\"#3a454a\"\n"
         "theme[used_start]=\"#a7c080\"\ntheme[available_mid]=\"#dbbc7f\"\ntheme[available_end]=\"#e67e80\"\n"}
    };
    
    for (const auto& t : defaults) {
        std::filesystem::path p = theme_dir / t.name;
        if (!std::filesystem::exists(p)) {
            std::ofstream f(p);
            f << t.content;
        }
    }
}

void TuiContext::refresh_themes_list() {
    theme_names.clear();
    char* home = getenv("HOME");
    std::filesystem::path theme_dir = std::string(home ? home : ".") + "/.config/mstorage/themes";
    
    ensure_default_themes(theme_dir);
    
    if (std::filesystem::exists(theme_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(theme_dir)) {
            if (entry.path().extension() == ".conf") {
                theme_names.push_back(entry.path().stem().string());
            }
        }
    }
    std::sort(theme_names.begin(), theme_names.end());
    if (theme_names.empty()) {
        theme_names.push_back("default");
    }
    theme_selected = 0;
}

void TuiContext::apply_selected_theme() {
    if (theme_selected < 0 || theme_selected >= static_cast<int>(theme_names.size())) return;
    std::string theme_name = theme_names[theme_selected];
    char* home = getenv("HOME");
    std::filesystem::path theme_path = std::string(home ? home : ".") + "/.config/mstorage/themes/" + theme_name + ".conf";
    theme = ThemeManager::load_from_file(theme_path);
    
    std::filesystem::path default_conf_path = std::string(home ? home : ".") + "/.config/mstorage/themes/default.conf";
    std::error_code ec;
    std::filesystem::copy_file(theme_path, default_conf_path, std::filesystem::copy_options::overwrite_existing, ec);
}

ftxui::ButtonOption TuiContext::make_button_option(bool primary) {
    ftxui::ButtonOption opt;
    opt.transform = [this, primary](const ftxui::EntryState& s) {
        std::string label = s.label;
        if (s.focused) {
            label = "   ➤  " + label + "  ◄   ";
        } else {
            label = "      " + label + "      ";
        }
        auto element = ftxui::text(label) | ftxui::center;
        if (s.focused) {
            return element | ftxui::bold | ftxui::color(theme.selected_fg) | ftxui::bgcolor(theme.selected_bg) | ftxui::border;
        } else {
            if (primary) {
                return element | ftxui::color(theme.main_fg) | ftxui::border;
            } else {
                return element | ftxui::color(theme.inactive_fg) | ftxui::border;
            }
        }
    };
    return opt;
}

ftxui::MenuOption TuiContext::make_menu_option(std::function<void()> on_enter) {
    ftxui::MenuOption opt;
    opt.entries_option.transform = [this](const ftxui::EntryState& s) {
        auto element = ftxui::text(s.label);
        if (s.focused) {
            return element | ftxui::bold | ftxui::color(theme.selected_fg) | ftxui::bgcolor(theme.selected_bg);
        } else if (s.active) {
            return element | ftxui::bold | ftxui::color(theme.selected_fg);
        } else {
            return element | ftxui::color(theme.main_fg);
        }
    };
    if (on_enter) {
        opt.on_enter = on_enter;
    }
    return opt;
}

} // namespace mstorage::tui
