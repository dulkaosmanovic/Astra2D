// hello_window: the smallest Astra2D program.
//
// A game is a class derived from astra2d::Application. The constructor opens the window and the
// OpenGL context; run() calls the hooks below every frame until the window closes or quit() is
// called. Everything else in the engine is built on this loop.
//
// Controls: Escape quits.

#include <astra2d/core/application.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/core/log.hpp>

#include <cmath>
#include <exception>

namespace {

class HelloWindow : public astra2d::Application {
public:
    HelloWindow()
        : Application({.window = {.title = "Astra2D: hello window", .width = 800, .height = 450}})
    {
    }

protected:
    // Once, after the window exists and before the first frame.
    void onStart() override { ASTRA_INFO("Hello from Astra2D. Press Escape to quit."); }

    // Once per frame with the real frame time: input, animation, everything that is not physics.
    void onUpdate(float deltaTime) override
    {
        m_time += deltaTime;
        if (input().isPressed(astra2d::Key::Escape)) {
            quit();
        }
    }

    // Once per frame, after onUpdate(). Here only the background colour, slowly changing.
    void onRender() override
    {
        const float red = 0.5F + (0.5F * std::sin(m_time));
        astra2d::Renderer2D::clear({red * 0.3F, 0.15F, 0.3F, 1.0F});
    }

private:
    float m_time = 0.0F;
};

} // namespace

int main()
{
    try {
        HelloWindow app;
        app.run();
    } catch (const std::exception& error) {
        // E.g. no OpenGL 3.3 driver: report it instead of terminating.
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
