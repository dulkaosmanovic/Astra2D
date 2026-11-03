#include "astra2d/core/window.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <stdexcept>

namespace astra2d {

namespace {

void glfwErrorCallback(int code, const char* description)
{
    spdlog::error("GLFW error {}: {}", code, description);
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

    spdlog::info("OpenGL {} ({})",
                 reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
}

Window::~Window()
{
    glfwDestroyWindow(m_handle);
    glfwTerminate();
}

void Window::pollEvents()
{
    glfwPollEvents();
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

int Window::framebufferWidth() const
{
    int width = 0;
    glfwGetFramebufferSize(m_handle, &width, nullptr);
    return width;
}

int Window::framebufferHeight() const
{
    int height = 0;
    glfwGetFramebufferSize(m_handle, nullptr, &height);
    return height;
}

} // namespace astra2d
