#include "astra2d/core/application.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace astra2d {

Application::Application(const WindowSettings& settings)
    : m_window(std::make_unique<Window>(settings))
{
}

Application::~Application() = default;

void Application::run()
{
    onStart();

    double lastTime = glfwGetTime();

    while (!m_window->shouldClose()) {
        const double now = glfwGetTime();
        const auto deltaTime = static_cast<float>(now - lastTime);
        lastTime = now;

        m_window->pollEvents();

        glViewport(0, 0, m_window->framebufferWidth(), m_window->framebufferHeight());

        onUpdate(deltaTime);
        onRender();

        m_window->swapBuffers();
    }

    onShutdown();
}

void Application::quit()
{
    m_window->requestClose();
}

} // namespace astra2d
