#pragma once

// OpenGL debug output: the driver reports errors and warnings at the call that caused them.
// Internal to the engine; used by Window when it creates the context.

namespace astra2d::detail {

#ifdef NDEBUG
inline constexpr bool kGlDebugOutput = false;
#else
inline constexpr bool kGlDebugOutput = true; ///< Request a debug context and log its messages.
#endif

/// Logs the driver's OpenGL messages through the engine logger, if it supports KHR_debug. Needs
/// the context to be current.
void enableGlDebugOutput();

} // namespace astra2d::detail
