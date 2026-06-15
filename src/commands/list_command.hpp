#pragma once
#include "command.hpp"
#include "moodle/moodle_client.hpp"
#include "core/session_manager.hpp"
#include <iostream>
#include <iomanip>

namespace mstorage::commands {

class ListCommand : public Command {
public:
    ListCommand(moodle::MoodleClient& moodle_client, core::SessionManager& session_manager)
        : moodle_client_(moodle_client), session_manager_(session_manager) {}

    std::expected<void, std::error_code> execute() override {
        auto session = session_manager_.load();
        if (!session) return std::unexpected(session.error());

        std::cout << std::left << std::setw(60) << "Filepath" << std::setw(15) << "Size" << "Type" << "\n";
        std::cout << std::string(90, '-') << "\n";
        
        auto res = list_recursive(session->cookie, "/", 0);
        if (!res) return std::unexpected(res.error());
        
        return {};
    }

private:
    std::expected<void, std::error_code> list_recursive(const std::string& cookie, const std::string& path, int depth) {
        auto files = moodle_client_.list_files(cookie, path);
        if (!files) return std::unexpected(files.error());

        for (const auto& file : *files) {
            std::string indent(depth * 2, ' ');

            if (file.filename == ".") {
                // It's a folder (Moodle represents folders with filename ".")
                if (file.filepath != "/") { // Skip the root folder itself
                    std::string display_name = truncate(indent + "📁 " + get_folder_name(file.filepath), 60);
                    print_cell(display_name, 62);
                    print_cell("-", 15);
                    std::cout << "DIR\n";

                    // Recurse into this folder
                    auto res = list_recursive(cookie, file.filepath, depth + 1);
                    if (!res) return res;
                }
            } else {
                // Regular file
                std::string display_name = truncate(indent + "📄 " + file.filename, 60);
                print_cell(display_name, 62);
                print_cell(file.size_f, 15);
                std::cout << "FILE\n";
            }
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
            // Simple UTF-8 character count (ignoring combining marks for simplicity)
            if ((c & 0xc0) != 0x80) {
                width++;
            }
        }
        // Patch for emojis (which usually take 2 cells in most modern terminals)
        // Since the code point counting gives 1, we add 1 extra for each emoji
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
        // We use a rough estimate for truncation based on display_width
        if (display_width(str) > static_cast<int>(width)) {
            // Surgical truncation: find a safe byte index
            size_t bytes = 0;
            int current_width = 0;
            while (bytes < str.length() && current_width < static_cast<int>(width) - 3) {
                unsigned char c = str[bytes];
                if ((c & 0xc0) != 0x80) current_width++;
                // Emoji patch in truncation too
                if (str.substr(bytes).starts_with("📁") || str.substr(bytes).starts_with("📄")) current_width++;

                // Move to next UTF-8 character
                if (c < 0x80) bytes += 1;
                else if ((c & 0xe0) == 0xc0) bytes += 2;
                else if ((c & 0xf0) == 0xe0) bytes += 3;
                else if ((c & 0xf8) == 0xf0) bytes += 4;
                else bytes += 1; // Should not happen
            }
            return str.substr(0, bytes) + "...";
        }
        return str;
    }

    std::string get_folder_name(const std::string& filepath) {
        if (filepath == "/") return "/";
        // Extract the last part of "/path/to/folder/"
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
