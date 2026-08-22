#include "core/Logger.h"

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace voterpool::Logger {
namespace {

spdlog::level::level_enum parseLevel(const std::string& lvl) {
    if (lvl == "trace") return spdlog::level::trace;
    if (lvl == "debug") return spdlog::level::debug;
    if (lvl == "info") return spdlog::level::info;
    if (lvl == "warn") return spdlog::level::warn;
    if (lvl == "error") return spdlog::level::err;
    if (lvl == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

}  // namespace

void init(const LoggingConfig& cfg) {
    if (cfg.async) {
        spdlog::init_thread_pool(cfg.async_queue_size, 1);
        auto sink = cfg.log_file.empty()
                        ? std::static_pointer_cast<spdlog::sinks::sink>(
                              std::make_shared<spdlog::sinks::stdout_color_sink_mt>())
                        : std::static_pointer_cast<spdlog::sinks::sink>(
                              std::make_shared<spdlog::sinks::basic_file_sink_mt>(cfg.log_file));
        auto logger = std::make_shared<spdlog::async_logger>(
            "voterpool", sink, spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);
        logger->set_level(parseLevel(cfg.level));
        logger->set_pattern(cfg.format);
        spdlog::set_default_logger(logger);
    } else {
        auto sink = cfg.log_file.empty()
                        ? std::static_pointer_cast<spdlog::sinks::sink>(
                              std::make_shared<spdlog::sinks::stdout_color_sink_mt>())
                        : std::static_pointer_cast<spdlog::sinks::sink>(
                              std::make_shared<spdlog::sinks::basic_file_sink_mt>(cfg.log_file));
        auto logger = std::make_shared<spdlog::logger>("voterpool", sink);
        logger->set_level(parseLevel(cfg.level));
        logger->set_pattern(cfg.format);
        spdlog::set_default_logger(logger);
    }
}

void shutdown() { spdlog::shutdown(); }

}  // namespace voterpool::Logger
