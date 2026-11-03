#include "astra2d/core/application.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace astra2d {

Application::Application(const ApplicationSettings& settings)
    : m_window(std::make_unique<Window>(settings.window))
    , m_timestep(settings.fixedTimestep)
{
}

Application::~Application() = default;

void Application::run()
{
    glViewport(0, 0, m_window->framebufferWidth(), m_window->framebufferHeight());
    onStart();

    double lastTime = glfwGetTime();

    while (!m_window->shouldClose()) {
        const double now = glfwGetTime();
        const double frameTime = now - lastTime;
        lastTime = now;

        m_window->input().beginFrame();
        m_window->pollEvents();

        if (m_window->isMinimized()) {
            // Nothing is visible: sleep until an event arrives instead of spinning, and don't
            // count the time spent minimized as simulation time.
            m_window->waitEvents();
            lastTime = glfwGetTime();
            continue;
        }

        if (m_window->consumeResize()) {
            const int width = m_window->framebufferWidth();
            const int height = m_window->framebufferHeight();
            glViewport(0, 0, width, height);
            onResize(width, height);
        }

        const int steps = m_timestep.advance(frameTime);
        const auto fixedDeltaTime = static_cast<float>(m_timestep.step());
        for (int i = 0; i < steps; ++i) {
            onFixedUpdate(fixedDeltaTime);
        }

        onUpdate(static_cast<float>(frameTime));

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
