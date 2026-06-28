#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <vector>

namespace mstorage::utils {

class Logger {
public:
    static void init() {
        char* home = getenv("HOME");
        auto share_path = std::filesystem::path(home ? home : ".") / ".local" / "share" / "mstorage";
        std::filesystem::create_directories(share_path);
        auto log_file = share_path / "mstorage.log";

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file.string(), false);
        file_sink->set_level(spdlog::level::debug);

        std::vector<spdlog::sink_ptr> sinks {file_sink};
        auto logger = std::make_shared<spdlog::logger>("mstorage", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::debug);
        
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::debug);
    }
};

} // namespace mstorage::utils
