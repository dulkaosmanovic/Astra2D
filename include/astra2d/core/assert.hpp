#pragma once

#include <spdlog/common.h>

#include <string_view>

/// @file
/// Assertion macros.
///
/// `ASTRA_ASSERT(condition)` or `ASTRA_ASSERT(condition, "format {}", args...)` logs the failed
/// condition, its location and an optional formatted message, then aborts.
///
/// Assertions are enabled when `NDEBUG` is not defined (Meson: `-Db_ndebug=false`, the default for
/// non-release builds) or when `ASTRA_ENABLE_ASSERTS` is defined. When disabled, the condition is
/// not evaluated (but still type-checked), so it must not have side effects.

/// @addtogroup core
/// @{

namespace astra2d::detail {

/// Logs an assertion failure and aborts the program. Used by ASTRA_ASSERT.
[[noreturn]] void assertFailed(const char* expression, const char* file, int line,
                               std::string_view message = {});

} // namespace astra2d::detail

#if defined(ASTRA_ENABLE_ASSERTS) || !defined(NDEBUG)
/// 1 when ASTRA_ASSERT is active in this build, 0 otherwise.
#define ASTRA_ASSERTS_ENABLED 1
#else
#define ASTRA_ASSERTS_ENABLED 0
#endif

#if ASTRA_ASSERTS_ENABLED
/// Aborts with a log message if @p condition is false. Optional arguments form a message with the
/// same `{}` syntax and formatter as the log macros:
/// `ASTRA_ASSERT(i < size, "index {} out of range", i)`. Compiled out in release builds.
#define ASTRA_ASSERT(condition, ...)                                            \
    do {                                                                        \
        if (!(condition)) [[unlikely]] {                                        \
            ::astra2d::detail::assertFailed(                                    \
                #condition, __FILE__,                                           \
                __LINE__ __VA_OPT__(, ::spdlog::fmt_lib::format(__VA_ARGS__))); \
        }                                                                       \
    } while (false)
#else
// sizeof keeps the condition type-checked and its variables "used", without evaluating it.
#define ASTRA_ASSERT(condition, ...) \
    do {                             \
        (void)sizeof(!(condition));  \
    } while (false)
#endif

/// @}
