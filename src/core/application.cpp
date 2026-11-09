#include "astra2d/core/application.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>

namespace astra2d {

Application::Application(const ApplicationSettings& settings)
    : m_window(std::make_unique<Window>(settings.window))
    , m_timestep(settings.fixedTimestep, std::max(settings.maxFrameTime, settings.fixedTimestep))
{
    m_window->setEventDispatcher(&m_events);
}

Application::~Application() = default;

void Application::run()
{
    // Another Window created after this one would have made its own context current.
    m_window->makeContextCurrent();
    glViewport(0, 0, m_window->framebufferWidth(), m_window->framebufferHeight());
    onStart();

    double lastTime = glfwGetTime();

    while (!m_window->shouldClose()) {
        m_window->pollEvents();

        if (m_window->isMinimized()) {
            // Nothing is visible: sleep until the window is restored instead of spinning. Events
            // that arrive meanwhile stay in the current input frame (waitEvents() does not start
            // a new one), and this frame then runs normally, so they reach onUpdate(). The time
            // spent minimized is not simulated.
            while (m_window->isMinimized() && !m_window->shouldClose()) {
                m_window->waitEvents();
            }
            if (m_window->shouldClose()) {
                break;
            }
            lastTime = glfwGetTime();
        }

        // Window events queued while polling (focus, gamepads) reach their handlers here, at a
        // fixed point of the frame instead of in the middle of GLFW's callbacks.
        m_events.dispatchQueued();

        const double now = glfwGetTime();
        const double frameTime = std::clamp(now - lastTime, 0.0, m_timestep.maxFrameTime());
        lastTime = now;

        if (m_window->consumeResize()) {
            const int width = m_window->framebufferWidth();
            const int height = m_window->framebufferHeight();
            glViewport(0, 0, width, height);
            onResize(width, height);
            m_events.publish(WindowResizeEvent{.width = width, .height = height});
        }

        const int steps = m_timestep.advance(frameTime);
        const auto fixedDeltaTime = static_cast<float>(m_timestep.step());
        Input& in = m_window->input();
        for (int i = 0; i < steps; ++i) {
            // Pressed/released inside a step report what happened since the previous step.
            in.beginFixedStep();
            onFixedUpdate(fixedDeltaTime);
            in.endFixedStep();
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
