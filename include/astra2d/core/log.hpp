#pragma once

#include <spdlog/spdlog.h>

/// @file
/// Logging for the engine and for games built on it.
///
/// Two named loggers are used so the source of a message is visible in the output:
/// - `ASTRA` for engine code (`ASTRA_CORE_*` macros),
/// - `GAME` for application code (`ASTRA_*` macros).
///
/// The loggers are created on first use, so logging works without any explicit
/// initialization (also in unit tests). Arguments use the `{}` format syntax:
/// @code
/// ASTRA_INFO("Loaded level {} with {} entities", name, count);
/// @endcode

/// @addtogroup core
/// @{

/// Engine and game loggers. Usually used through the `ASTRA_*` macros.
namespace astra2d::log {

/// Logger used by the engine itself.
[[nodiscard]] spdlog::logger& engine();

/// Logger intended for games and tools built on the engine.
[[nodiscard]] spdlog::logger& game();

/// Sets the minimum level for both loggers.
void setLevel(spdlog::level::level_enum level);

} // namespace astra2d::log

// clang-format off
/// Logs a trace message from the engine (`ASTRA`) logger.
#define ASTRA_CORE_TRACE(...)    ::astra2d::log::engine().trace(__VA_ARGS__)
/// Logs a debug message from the engine (`ASTRA`) logger.
#define ASTRA_CORE_DEBUG(...)    ::astra2d::log::engine().debug(__VA_ARGS__)
/// Logs an info message from the engine (`ASTRA`) logger.
#define ASTRA_CORE_INFO(...)     ::astra2d::log::engine().info(__VA_ARGS__)
/// Logs a warning message from the engine (`ASTRA`) logger.
#define ASTRA_CORE_WARN(...)     ::astra2d::log::engine().warn(__VA_ARGS__)
/// Logs an error message from the engine (`ASTRA`) logger.
#define ASTRA_CORE_ERROR(...)    ::astra2d::log::engine().error(__VA_ARGS__)
/// Logs a critical message from the engine (`ASTRA`) logger.
#define ASTRA_CORE_CRITICAL(...) ::astra2d::log::engine().critical(__VA_ARGS__)

/// Logs a trace message from the game (`GAME`) logger.
#define ASTRA_TRACE(...)         ::astra2d::log::game().trace(__VA_ARGS__)
/// Logs a debug message from the game (`GAME`) logger.
#define ASTRA_DEBUG(...)         ::astra2d::log::game().debug(__VA_ARGS__)
/// Logs an info message from the game (`GAME`) logger.
#define ASTRA_INFO(...)          ::astra2d::log::game().info(__VA_ARGS__)
/// Logs a warning message from the game (`GAME`) logger.
#define ASTRA_WARN(...)          ::astra2d::log::game().warn(__VA_ARGS__)
/// Logs an error message from the game (`GAME`) logger.
#define ASTRA_ERROR(...)         ::astra2d::log::game().error(__VA_ARGS__)
/// Logs a critical message from the game (`GAME`) logger.
#define ASTRA_CRITICAL(...)      ::astra2d::log::game().critical(__VA_ARGS__)
// clang-format on

/// @}
