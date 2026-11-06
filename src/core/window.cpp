#include "astra2d/core/window.hpp"

#include "astra2d/core/events.hpp"
#include "astra2d/core/log.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <array>
#include <span>
#include <stdexcept>

// ---------------------------------------------------------------------------------------------
// LeakSanitizer: ignore leaks inside the GPU driver and GLFW's keyboard handling.
//
// - Creating the first OpenGL context makes the driver (e.g. Mesa radeonsi) allocate global state
//   once (in a pthread_once) that it keeps until the process exits, even after glfwTerminate().
//   The driver is unloaded by then, so its frames show as "<unknown module>" and only the GLFW
//   call that created the context can be matched.
// - GLFW 3.4's Wayland backend builds new xkbcommon keymap and compose state whenever the
//   keyboard layout changes (another layout, a virtual keyboard) without freeing the old one.
//
// Both are in system libraries the engine cannot fix. Only stacks through glfwCreateWindow or
// libxkbcommon (which the engine never calls) are hidden, so leaks in engine or game code are
// still reported. The sanitizer runtime calls this function if it exists; it is defined here
// because window.cpp is always linked.
#ifdef __SANITIZE_ADDRESS__
#define ASTRA2D_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ASTRA2D_ASAN 1
#endif
#endif

#ifdef ASTRA2D_ASAN
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern "C" const char* __lsan_default_suppressions()
{
    return "leak:glfwCreateWindow\n"
           "leak:libxkbcommon.so\n";
}
#endif

namespace astra2d {

// Key and MouseButton values are GLFW codes, so events can be forwarded without translation.
static_assert(static_cast<int>(Key::Space) == GLFW_KEY_SPACE);
static_assert(static_cast<int>(Key::Num9) == GLFW_KEY_9);
static_assert(static_cast<int>(Key::Z) == GLFW_KEY_Z);
static_assert(static_cast<int>(Key::Escape) == GLFW_KEY_ESCAPE);
static_assert(static_cast<int>(Key::Up) == GLFW_KEY_UP);
static_assert(static_cast<int>(Key::End) == GLFW_KEY_END);
static_assert(static_cast<int>(Key::F12) == GLFW_KEY_F12);
static_assert(static_cast<int>(Key::LeftShift) == GLFW_KEY_LEFT_SHIFT);
static_assert(static_cast<int>(Key::Menu) == GLFW_KEY_LAST);
static_assert(static_cast<int>(MouseButton::Left) == GLFW_MOUSE_BUTTON_LEFT);
static_assert(static_cast<int>(MouseButton::Right) == GLFW_MOUSE_BUTTON_RIGHT);
static_assert(static_cast<int>(MouseButton::Middle) == GLFW_MOUSE_BUTTON_MIDDLE);
static_assert(static_cast<int>(MouseButton::Button8) == GLFW_MOUSE_BUTTON_LAST);
static_assert(static_cast<int>(GamepadButton::A) == GLFW_GAMEPAD_BUTTON_A);
static_assert(static_cast<int>(GamepadButton::Guide) == GLFW_GAMEPAD_BUTTON_GUIDE);
static_assert(static_cast<int>(GamepadButton::DpadLeft) == GLFW_GAMEPAD_BUTTON_LAST);
static_assert(static_cast<int>(GamepadAxis::LeftY) == GLFW_GAMEPAD_AXIS_LEFT_Y);
static_assert(static_cast<int>(GamepadAxis::RightTrigger) == GLFW_GAMEPAD_AXIS_LAST);
static_assert(Input::kMaxGamepads <= GLFW_JOYSTICK_LAST + 1);
static_assert(static_cast<int>(Key::World2) == GLFW_KEY_WORLD_2);
static_assert(static_cast<int>(Key::CapsLock) == GLFW_KEY_CAPS_LOCK);
static_assert(static_cast<int>(Key::Pause) == GLFW_KEY_PAUSE);
static_assert(static_cast<int>(Key::F25) == GLFW_KEY_F25);
static_assert(static_cast<int>(Key::Keypad0) == GLFW_KEY_KP_0);
static_assert(static_cast<int>(Key::KeypadEqual) == GLFW_KEY_KP_EQUAL);

namespace {

void glfwErrorCallback(int code, const char* description)
{
    ASTRA_CORE_ERROR("GLFW error {}: {}", code, description);
}

Window& windowFrom(GLFWwindow* handle)
{
    return *static_cast<Window*>(glfwGetWindowUserPointer(handle));
}

// glGetString returns nullptr on error, which the formatter would reject.
const char* glString(GLenum name)
{
    const GLubyte* value = glGetString(name);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): GL returns GLubyte* strings
    return value != nullptr ? reinterpret_cast<const char*>(value) : "unknown";
}

// Number of live Window objects. Mutable global state on purpose: GLFW itself is a process-wide,
// main-thread-only library, so one counter guards its glfwInit()/glfwTerminate().
int g_glfwUsers = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// ---------------------------------------------------------------------------------------------
// OpenGL debug output (debug builds only)
//
// The driver reports errors and warnings through a callback, at the call that caused them, so a
// wrong OpenGL call is logged immediately instead of silently doing nothing. The GLAD loader is
// generated for OpenGL 3.3, which predates this API (core in 4.3, or the KHR_debug extension), so
// the one function needed is looked up through GLFW when the driver provides it.

#ifdef NDEBUG
constexpr bool kGlDebugOutput = false;
#else
constexpr bool kGlDebugOutput = true;
#endif

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

} // namespace

Window::GlfwLibrary::GlfwLibrary()
{
    if (g_glfwUsers == 0) {
        glfwSetErrorCallback(glfwErrorCallback);
        if (glfwInit() == GLFW_FALSE) {
            throw std::runtime_error("Failed to initialize GLFW");
        }
    }
    ++g_glfwUsers;
}

Window::GlfwLibrary::~GlfwLibrary()
{
    if (--g_glfwUsers == 0) {
        glfwTerminate();
    }
}

void Window::HandleDeleter::operator()(GLFWwindow* handle) const noexcept
{
    glfwDestroyWindow(handle);
}

Window::Window(const WindowSettings& settings)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, settings.resizable ? GLFW_TRUE : GLFW_FALSE);
    // Drivers only promise to report problems in debug contexts.
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, kGlDebugOutput ? GLFW_TRUE : GLFW_FALSE);

    m_handle.reset(glfwCreateWindow(settings.width, settings.height, settings.title.c_str(),
                                    nullptr, nullptr));
    if (!m_handle) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(m_handle.get());

    if (gladLoadGL(glfwGetProcAddress) == 0) { // returns the loaded GL version, 0 on failure
        throw std::runtime_error("Failed to load OpenGL functions");
    }

    if (kGlDebugOutput) { // a plain if keeps the function referenced in release builds
        enableGlDebugOutput();
    }

    glfwSwapInterval(settings.vsync ? 1 : 0);

    glfwGetFramebufferSize(m_handle.get(), &m_framebufferWidth, &m_framebufferHeight);
    updateCursorScale();

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(m_handle.get(), &cursorX, &cursorY);
    m_input.onMouseMove(toFramebuffer(cursorX, cursorY));
    m_input.beginFrame(); // no mouse delta on the first frame

    installCallbacks();

    ASTRA_CORE_INFO("OpenGL {} ({})", glString(GL_VERSION), glString(GL_RENDERER));
}

Window::~Window() = default;

void Window::updateCursorScale()
{
    int width = 0;
    int height = 0;
    glfwGetWindowSize(m_handle.get(), &width, &height);
    // While minimized both sizes are 0; keep the last scale.
    if (width > 0 && height > 0 && m_framebufferWidth > 0 && m_framebufferHeight > 0) {
        m_cursorScale = {static_cast<float>(m_framebufferWidth) / static_cast<float>(width),
                         static_cast<float>(m_framebufferHeight) / static_cast<float>(height)};
    }
}

glm::vec2 Window::toFramebuffer(double x, double y) const
{
    return glm::vec2(static_cast<float>(x), static_cast<float>(y)) * m_cursorScale;
}

void Window::installCallbacks()
{
    glfwSetWindowUserPointer(m_handle.get(), this);

    glfwSetKeyCallback(m_handle.get(),
                       [](GLFWwindow* handle, int key, int /*scancode*/, int action, int /*mods*/) {
        if (action == GLFW_PRESS || action == GLFW_RELEASE) {
            windowFrom(handle).m_input.onKey(key, action == GLFW_PRESS);
        }
    });

    glfwSetMouseButtonCallback(m_handle.get(),
                               [](GLFWwindow* handle, int button, int action, int /*mods*/) {
        windowFrom(handle).m_input.onMouseButton(button, action == GLFW_PRESS);
    });

    glfwSetCursorPosCallback(m_handle.get(), [](GLFWwindow* handle, double x, double y) {
        Window& window = windowFrom(handle);
        window.m_input.onMouseMove(window.toFramebuffer(x, y));
    });

    glfwSetScrollCallback(m_handle.get(), [](GLFWwindow* handle, double x, double y) {
        windowFrom(handle).m_input.onScroll({static_cast<float>(x), static_cast<float>(y)});
    });

    glfwSetWindowFocusCallback(m_handle.get(), [](GLFWwindow* handle, int focused) {
        // Release events are not delivered to an unfocused window, so keys would stay "held".
        // Keys already down when focus returns are not reported until they are pressed again.
        Window& window = windowFrom(handle);
        if (!focused) {
            window.m_input.releaseAll();
        }
        if (window.m_events != nullptr) {
            window.m_events->enqueue(WindowFocusEvent{.focused = focused == GLFW_TRUE});
        }
    });

    glfwSetFramebufferSizeCallback(m_handle.get(), [](GLFWwindow* handle, int width, int height) {
        Window& window = windowFrom(handle);
        window.m_framebufferWidth = width;
        window.m_framebufferHeight = height;
        window.m_resized = true;
        window.updateCursorScale();
    });

    // Moving to a monitor with a different scale changes the window size without necessarily
    // changing the framebuffer size, so the cursor scale is updated here too.
    glfwSetWindowSizeCallback(m_handle.get(),
                              [](GLFWwindow* handle, int /*width*/, int /*height*/) {
        windowFrom(handle).updateCursorScale();
    });
}

void Window::pollEvents()
{
    m_input.beginFrame();
    glfwPollEvents();
    pollGamepads();
}

void Window::pollGamepads()
{
    // GLFW reports gamepads as state to poll, not as events. Joysticks without a gamepad mapping
    // (GLFW's SDL_GameControllerDB copy) are not used.
    for (int index = 0; index < Input::kMaxGamepads; ++index) {
        const int joystick = GLFW_JOYSTICK_1 + index;
        const bool wasConnected = m_input.isGamepadConnected(index);
        GLFWgamepadstate state{};
        const bool connected = glfwJoystickIsGamepad(joystick) == GLFW_TRUE &&
                               glfwGetGamepadState(joystick, &state) == GLFW_TRUE;
        if (connected) {
            std::array<bool, Input::kGamepadButtonCount> buttons{};
            for (std::size_t i = 0; i < buttons.size(); ++i) {
                buttons.at(i) = state.buttons[i] == GLFW_PRESS; // NOLINT(*-constant-array-index)
            }
            m_input.onGamepadState(index, buttons, std::span<const float, 6>(state.axes));
        } else if (wasConnected) {
            m_input.onGamepadDisconnected(index);
        }
        if (connected != wasConnected) {
            ASTRA_CORE_INFO("Gamepad {} {}{}", index, connected ? "connected: " : "disconnected",
                            connected ? glfwGetGamepadName(joystick) : "");
            if (m_events != nullptr) {
                m_events->enqueue(GamepadConnectionEvent{.gamepad = index, .connected = connected});
            }
        }
    }
}

void Window::waitEvents()
{
    glfwWaitEvents();
}

void Window::swapBuffers()
{
    glfwSwapBuffers(m_handle.get());
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(m_handle.get()) == GLFW_TRUE;
}

void Window::requestClose()
{
    glfwSetWindowShouldClose(m_handle.get(), GLFW_TRUE);
}

void Window::makeContextCurrent()
{
    glfwMakeContextCurrent(m_handle.get());
}

bool Window::isContextCurrent() const
{
    return glfwGetCurrentContext() == m_handle.get();
}

void Window::setVsync(bool enabled)
{
    // The swap interval belongs to the current context, so switch to this window's context for
    // the call and back afterwards; another window that is current keeps drawing into itself.
    GLFWwindow* previous = glfwGetCurrentContext();
    glfwMakeContextCurrent(m_handle.get());
    glfwSwapInterval(enabled ? 1 : 0);
    glfwMakeContextCurrent(previous);
}

void Window::setTitle(const std::string& title)
{
    glfwSetWindowTitle(m_handle.get(), title.c_str());
}

float Window::aspectRatio() const
{
    if (isMinimized()) {
        return 1.0F;
    }
    return static_cast<float>(m_framebufferWidth) / static_cast<float>(m_framebufferHeight);
}

bool Window::consumeResize()
{
    const bool resized = m_resized;
    m_resized = false;
    return resized;
}

} // namespace astra2d
