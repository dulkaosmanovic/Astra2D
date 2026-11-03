#include "astra2d/core/log.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>

namespace astra2d::log {

namespace {

std::shared_ptr<spdlog::logger> makeLogger(const char* name)
{
    // stderr keeps stdout free for program output (e.g. benchmark CSV).
    auto logger = spdlog::stderr_color_mt(name);
    logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
    logger->set_level(spdlog::level::trace);
    // Errors are flushed immediately so they are not lost if the program aborts.
    logger->flush_on(spdlog::level::err);
    return logger;
}

} // namespace

spdlog::logger& engine()
{
    static const std::shared_ptr<spdlog::logger> logger = makeLogger("ASTRA");
    return *logger;
}

spdlog::logger& game()
{
    static const std::shared_ptr<spdlog::logger> logger = makeLogger("GAME");
    return *logger;
}

void setLevel(spdlog::level::level_enum level)
{
    engine().set_level(level);
    game().set_level(level);
}

} // namespace astra2d::log
