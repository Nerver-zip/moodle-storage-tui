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
        auto config_path = std::filesystem::path(getenv("HOME")) / ".config" / "mstorage";
        std::filesystem::create_directories(config_path);
        auto log_file = config_path / "mstorage.log";

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::info);

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file.string(), true);
        file_sink->set_level(spdlog::level::debug);

        std::vector<spdlog::sink_ptr> sinks {console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("mstorage", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::debug);
        
        spdlog::set_default_logger(logger);
    }
};

} // namespace mstorage::utils
