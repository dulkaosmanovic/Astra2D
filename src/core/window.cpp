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

namespace {

void glfwErrorCallback(int code, const char* description)
{
    ASTRA_CORE_ERROR("GLFW error {}: {}", code, description);
}

Window& windowFrom(GLFWwindow* handle)
{
    return *static_cast<Window*>(glfwGetWindowUserPointer(handle));
}

} // namespace

Window::Window(const WindowSettings& settings)
{
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, settings.resizable ? GLFW_TRUE : GLFW_FALSE);

    m_handle = glfwCreateWindow(settings.width, settings.height, settings.title.c_str(), nullptr, nullptr);
    if (!m_handle) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(m_handle);

    if (!gladLoadGL(glfwGetProcAddress)) {
        glfwDestroyWindow(m_handle);
        glfwTerminate();
        throw std::runtime_error("Failed to load OpenGL functions");
    }

    glfwSwapInterval(settings.vsync ? 1 : 0);

    glfwGetFramebufferSize(m_handle, &m_framebufferWidth, &m_framebufferHeight);

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(m_handle, &cursorX, &cursorY);
    m_input.onMouseMove({static_cast<float>(cursorX), static_cast<float>(cursorY)});
    m_input.beginFrame(); // no mouse delta on the first frame

    installCallbacks();

    ASTRA_CORE_INFO("OpenGL {} ({})",
                    reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                    reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
}

Window::~Window()
{
    glfwDestroyWindow(m_handle);
    glfwTerminate();
}

void Window::installCallbacks()
{
    glfwSetWindowUserPointer(m_handle, this);

    glfwSetKeyCallback(m_handle, [](GLFWwindow* handle, int key, int /*scancode*/, int action, int /*mods*/) {
        if (action == GLFW_PRESS || action == GLFW_RELEASE) {
            windowFrom(handle).m_input.onKey(key, action == GLFW_PRESS);
        }
    });

    glfwSetMouseButtonCallback(m_handle, [](GLFWwindow* handle, int button, int action, int /*mods*/) {
        windowFrom(handle).m_input.onMouseButton(button, action == GLFW_PRESS);
    });

    glfwSetCursorPosCallback(m_handle, [](GLFWwindow* handle, double x, double y) {
        windowFrom(handle).m_input.onMouseMove({static_cast<float>(x), static_cast<float>(y)});
    });

    glfwSetScrollCallback(m_handle, [](GLFWwindow* handle, double x, double y) {
        windowFrom(handle).m_input.onScroll({static_cast<float>(x), static_cast<float>(y)});
    });

    glfwSetWindowFocusCallback(m_handle, [](GLFWwindow* handle, int focused) {
        // Release events are not delivered to an unfocused window, so keys would stay "held".
        if (!focused) {
            windowFrom(handle).m_input.releaseAll();
        }
    });

    glfwSetFramebufferSizeCallback(m_handle, [](GLFWwindow* handle, int width, int height) {
        Window& window = windowFrom(handle);
        window.m_framebufferWidth = width;
        window.m_framebufferHeight = height;
        window.m_resized = true;
    });
}

void Window::pollEvents()
{
    glfwPollEvents();
}

void Window::waitEvents()
{
    glfwWaitEvents();
}

void Window::swapBuffers()
{
    glfwSwapBuffers(m_handle);
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(m_handle);
}

void Window::requestClose()
{
    glfwSetWindowShouldClose(m_handle, GLFW_TRUE);
}

void Window::setTitle(const std::string& title)
{
    glfwSetWindowTitle(m_handle, title.c_str());
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
