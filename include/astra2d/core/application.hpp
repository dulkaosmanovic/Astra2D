#pragma once

#include "astra2d/core/events.hpp"
#include "astra2d/core/input.hpp"
#include "astra2d/core/timestep.hpp"
#include "astra2d/core/window.hpp"

#include <memory>

/// @addtogroup core
/// @{

namespace astra2d {

/// Settings used when creating an Application.
struct ApplicationSettings {
    /// Settings for the main window.
    WindowSettings window{};

    /// Length of one fixed update step in seconds (physics, deterministic gameplay). Must be > 0.
    double fixedTimestep = 1.0 / 60.0;

    /// Longest frame time in seconds passed to the simulation and to onUpdate(). Longer frames
    /// (a stall, a breakpoint) are clamped to this, so the game slows down instead of jumping.
    /// Values below fixedTimestep are raised to fixedTimestep.
    double maxFrameTime = 0.25;
};

/// Base class for games and tools built on Astra2D.
///
/// Derive from Application, override the hooks and call run():
/// @code
/// class MyGame : public astra2d::Application { ... };
/// int main() { MyGame game; game.run(); }
/// @endcode
///
/// Each frame of the main loop:
/// 1. window events are polled (Window::pollEvents() also starts a new Input frame and polls the
///    gamepads), then queued events() are dispatched;
/// 2. if the framebuffer was resized, the viewport is updated, onResize() is called and a
///    WindowResizeEvent is published;
/// 3. onFixedUpdate() runs zero or more times with a constant step (see FixedTimestep); in it,
///    Input's pressed/released report what happened since the previous step, so a press is seen
///    by exactly one step;
/// 4. onUpdate() runs once with the frame's game time: the real frame time times timeScale(),
///    clamped to ApplicationSettings::maxFrameTime (0 while paused);
/// 5. onRender() runs once, then buffers are swapped.
///
/// Each iteration is one frame of Profiler::global(), with the scopes "poll events",
/// "dispatch events", "fixed update" (once per step), "update", "render" and "swap buffers"
/// (which includes waiting for vsync); the game's own ASTRA_PROFILE_SCOPEs nest inside them.
///
/// While the window is minimized the loop sleeps until the next event and no updates run. Input
/// events received while minimized (e.g. the click that restores the window) are kept and reported
/// in the first frame after the window is restored.
class Application {
public:
    /// Creates the main window. Nothing else happens until run() is called.
    /// @throws std::runtime_error if the window or OpenGL context cannot be created.
    /// @throws std::invalid_argument if the timestep settings are out of range.
    explicit Application(const ApplicationSettings& settings = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    /// Runs the main loop until the window is closed or quit() is called.
    void run();

    /// Requests the main loop to stop after the current frame.
    void quit();

    /// The main window.
    [[nodiscard]] Window& window() { return *m_window; }
    /// @copydoc window()
    [[nodiscard]] const Window& window() const { return *m_window; }

    /// Keyboard, mouse and gamepad state for the current frame.
    [[nodiscard]] const Input& input() const { return m_window->input(); }

    /// The application's event dispatcher. The engine publishes WindowResizeEvent,
    /// WindowFocusEvent and GamepadConnectionEvent on it; games can use it for their own events.
    /// Queued events are dispatched once per frame, after the window events were polled.
    [[nodiscard]] EventDispatcher& events() { return m_events; }

    /// Length of one fixed update step in seconds.
    [[nodiscard]] double fixedTimestep() const { return m_timestep.step(); }

    /// Game seconds per real second, for debugging: 1 is normal, 0.25 slow motion, 0 pauses the
    /// game (no fixed steps; onUpdate() gets 0, so animations stop) while it is still drawn and
    /// gets input. Presses made while paused are not seen by the first step after it.
    /// @throws std::invalid_argument if @p scale is negative or not finite.
    void setTimeScale(double scale);
    /// Real seconds of the current frame (clamped to the maximum frame time), whatever the time
    /// scale: for frame statistics, not for gameplay (which uses onUpdate()'s game time).
    [[nodiscard]] float frameTime() const { return static_cast<float>(m_frameTime); }

    /// Game seconds per real second (0: paused).
    [[nodiscard]] double timeScale() const { return m_timestep.timeScale(); }
    /// While paused, runs exactly one fixed step in the next frame (onUpdate() then gets one
    /// step's time). Ignored while the game runs.
    void stepOnce() { m_timestep.requestStep(); }

    /// How far the current frame is between the last two fixed updates, in [0, 1).
    /// Can be used in onRender() to interpolate positions for smooth movement.
    [[nodiscard]] float interpolationAlpha() const
    {
        return static_cast<float>(m_timestep.alpha());
    }

protected:
    /// Called once before the first frame.
    virtual void onStart() {}

    /// Called at a fixed rate with fixedTimestep() seconds; use for physics and simulation.
    virtual void onFixedUpdate(float /*fixedDeltaTime*/) {}

    /// Called once per frame with the elapsed game time in seconds (scaled by timeScale(), clamped
    /// to the maximum frame time);
    /// use for input and animation.
    virtual void onUpdate(float /*deltaTime*/) {}

    /// Called once per frame after onUpdate().
    virtual void onRender() {}

    /// Called when the framebuffer size changes (in pixels); the viewport is already updated.
    virtual void onResize(int /*width*/, int /*height*/) {}

    /// Called once after the main loop ends.
    virtual void onShutdown() {}

private:
    // Declared first, so it is destroyed last: the window holds a pointer to it.
    EventDispatcher m_events;
    std::unique_ptr<Window> m_window;
    FixedTimestep m_timestep;
    double m_frameTime = 0.0;
};

} // namespace astra2d

/// @}
