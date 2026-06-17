#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace mstorage::commands {

class ListCommand : public Command {
public:
    ListCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager)
        : moodle_client_(moodle_client), session_manager_(session_manager) {}

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        std::cout << "Filepath                                                    Size           Type\n";
        std::cout << std::string(90, '-') << std::endl;

        // Print root directory visually
        print_cell("📁 /", 62);
        print_cell("-", 15);
        std::cout << "DIR\n";

        return list_recursive("/", 1);
    }

private:
    std::expected<void, std::error_code> list_recursive(const std::string& path, int indent) {
        auto files = moodle_client_.list_files("", path);
        if (!files) return std::unexpected(files.error());

        std::vector<models::MoodleFile> dirs, regular_files;
        for (const auto& f : *files) {
            if (f.filename == ".") dirs.push_back(f);
            else regular_files.push_back(f);
        }

        auto sorter = [](const models::MoodleFile& a, const models::MoodleFile& b) {
            return a.filepath < b.filepath;
        };
        std::sort(dirs.begin(), dirs.end(), sorter);
        std::sort(regular_files.begin(), regular_files.end(), [](const models::MoodleFile& a, const models::MoodleFile& b) {
            return a.filename < b.filename;
        });

        for (const auto& d : dirs) {
            std::string folder_name = get_folder_name(d.filepath);
            std::string prefix(indent * 4, ' ');
            std::string display_name = truncate(prefix + "📁 " + folder_name, 60);
            print_cell(display_name, 62);
            print_cell("-", 15);
            std::cout << "DIR\n";
            
            auto res = list_recursive(d.filepath, indent + 1);
            if (!res) return res;
        }

        for (const auto& f : regular_files) {
            std::string prefix(indent * 4, ' ');
            std::string display_name = truncate(prefix + "📄 " + f.filename, 60);
            print_cell(display_name, 62);
            print_cell(f.size_f, 15);
            std::cout << "FILE\n";
        }

        return {};
    }

    void print_cell(const std::string& str, int width) {
        std::cout << str;
        int actual = display_width(str);
        if (width > actual) {
            std::cout << std::string(width - actual, ' ');
        }
    }

    int display_width(const std::string& str) {
        int width = 0;
        for (size_t i = 0; i < str.length(); ++i) {
            unsigned char c = str[i];
            if ((c & 0xc0) != 0x80) {
                width++;
            }
        }
        auto count_occurrences = [&](const std::string& emoji) {
            int count = 0;
            size_t pos = 0;
            while ((pos = str.find(emoji, pos)) != std::string::npos) {
                count++;
                pos += emoji.length();
            }
            return count;
        };
        width += count_occurrences("📁");
        width += count_occurrences("📄");
        return width;
    }

    std::string truncate(std::string str, size_t width) {
        if (display_width(str) > static_cast<int>(width)) {
            size_t bytes = 0;
            int current_width = 0;
            while (bytes < str.length() && current_width < static_cast<int>(width) - 3) {
                unsigned char c = str[bytes];
                if ((c & 0xc0) != 0x80) current_width++;
                if (str.substr(bytes).starts_with("📁") || str.substr(bytes).starts_with("📄")) current_width++;

                if (c < 0x80) bytes += 1;
                else if ((c & 0xe0) == 0xc0) bytes += 2;
                else if ((c & 0xf0) == 0xe0) bytes += 3;
                else if ((c & 0xf8) == 0xf0) bytes += 4;
                else bytes += 1;
            }
            return str.substr(0, bytes) + "...";
        }
        return str;
    }

    std::string get_folder_name(const std::string& filepath) {
        if (filepath == "/") return "/";
        std::string clean_path = filepath;
        if (clean_path.back() == '/') clean_path.pop_back();
        auto pos = clean_path.find_last_of('/');
        if (pos != std::string::npos) {
            return clean_path.substr(pos + 1);
        }
        return clean_path;
    }

    moodle::MoodleClient& moodle_client_;
    core::SessionManager& session_manager_;
};

} // namespace mstorage::commands
