#pragma once

#include "astra2d/core/window.hpp"

#include <memory>

namespace astra2d {

/// Base class for games and tools built on Astra2D.
///
/// Derive from Application, override the hooks and call run():
/// @code
/// class MyGame : public astra2d::Application { ... };
/// int main() { MyGame game; game.run(); }
/// @endcode
class Application {
public:
    explicit Application(const WindowSettings& settings = {});
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Runs the main loop until the window is closed or quit() is called.
    void run();

    /// Requests the main loop to stop after the current frame.
    void quit();

    [[nodiscard]] Window& window() { return *m_window; }

protected:
    /// Called once before the first frame.
    virtual void onStart() {}

    /// Called once per frame with the elapsed time in seconds.
    virtual void onUpdate(float /*deltaTime*/) {}

    /// Called once per frame after onUpdate().
    virtual void onRender() {}

    /// Called once after the main loop ends.
    virtual void onShutdown() {}

private:
    std::unique_ptr<Window> m_window;
};

} // namespace astra2d
