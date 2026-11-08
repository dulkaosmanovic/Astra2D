#include "gl_debug.hpp"

#include "astra2d/core/log.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace astra2d::detail {

namespace {

// The driver reports errors and warnings through a callback, at the call that caused them, so a
// wrong OpenGL call is logged immediately instead of silently doing nothing. The GLAD loader is
// generated for OpenGL 3.3, which predates this API (core in 4.3, or the KHR_debug extension), so
// the one function needed is looked up through GLFW when the driver provides it.

constexpr GLenum kDebugOutput = 0x92E0;            // GL_DEBUG_OUTPUT
constexpr GLenum kDebugOutputSynchronous = 0x8242; // GL_DEBUG_OUTPUT_SYNCHRONOUS
constexpr GLenum kDebugSeverityHigh = 0x9146;      // GL_DEBUG_SEVERITY_HIGH
constexpr GLenum kDebugSeverityMedium = 0x9147;    // GL_DEBUG_SEVERITY_MEDIUM
constexpr GLenum kDebugSeverityLow = 0x9148;       // GL_DEBUG_SEVERITY_LOW

using DebugProc = void(GLAD_API_PTR*)(GLenum source, GLenum type, GLuint id, GLenum severity,
                                      GLsizei length, const GLchar* message, const void* user);
using DebugMessageCallbackProc = void(GLAD_API_PTR*)(DebugProc callback, const void* user);

void GLAD_API_PTR glDebugMessage(GLenum /*source*/, GLenum /*type*/, GLuint id, GLenum severity,
                                 GLsizei /*length*/, const GLchar* message, const void* /*user*/)
{
    switch (severity) {
    case kDebugSeverityHigh:
        ASTRA_CORE_ERROR("OpenGL [{}]: {}", id, message);
        break;
    case kDebugSeverityMedium:
        ASTRA_CORE_WARN("OpenGL [{}]: {}", id, message);
        break;
    case kDebugSeverityLow:
        ASTRA_CORE_INFO("OpenGL [{}]: {}", id, message);
        break;
    default:
        break; // notifications (e.g. "buffer will use video memory") would flood the log
    }
}

} // namespace

void enableGlDebugOutput()
{
    if (glfwExtensionSupported("GL_KHR_debug") == GLFW_FALSE) {
        ASTRA_CORE_DEBUG("OpenGL debug output not available (no KHR_debug)");
        return;
    }
    // GLFW returns a generic function pointer; it has to be cast to the real signature.
    const auto setCallback =
        reinterpret_cast<DebugMessageCallbackProc>( // NOLINT(*-reinterpret-cast)
            glfwGetProcAddress("glDebugMessageCallback"));
    if (setCallback == nullptr) {
        return;
    }
    glEnable(kDebugOutput);
    glEnable(kDebugOutputSynchronous); // report inside the offending call (useful in a debugger)
    setCallback(glDebugMessage, nullptr);
    ASTRA_CORE_DEBUG("OpenGL debug output enabled");
}

} // namespace astra2d::detail
