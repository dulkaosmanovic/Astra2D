#include "astra2d/core/window.hpp"

#include "astra2d/core/log.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <stdexcept>

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

    m_handle.reset(glfwCreateWindow(settings.width, settings.height, settings.title.c_str(),
                                    nullptr, nullptr));
    if (!m_handle) {
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(m_handle.get());

    if (gladLoadGL(glfwGetProcAddress) == 0) { // returns the loaded GL version, 0 on failure
        throw std::runtime_error("Failed to load OpenGL functions");
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
        if (!focused) {
            windowFrom(handle).m_input.releaseAll();
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
