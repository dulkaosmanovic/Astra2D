// shapes: drawing with Renderer2D and moving the camera.
//
// Everything is drawn in *world units* through an OrthographicCamera; the camera decides which
// part of the world is visible. Here 400 world units are visible vertically, whatever the window
// size, and the camera follows the mouse drag and wheel.
//
// Controls:
//   drag with the left mouse button   pan
//   mouse wheel                       zoom
//   Space                             reset the camera
//   Escape                            quit

#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/renderer2d.hpp>

#include <glad/gl.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>

namespace {

constexpr glm::vec4 kBackground{0.08F, 0.09F, 0.12F, 1.0F};
constexpr glm::vec4 kGrid{1.0F, 1.0F, 1.0F, 0.06F};
constexpr glm::vec4 kAxisX{0.9F, 0.3F, 0.3F, 0.8F};
constexpr glm::vec4 kAxisY{0.3F, 0.9F, 0.4F, 0.8F};

class Shapes : public astra2d::Application {
public:
    Shapes()
        : Application({.window = {.title = "Astra2D: shapes", .width = 960, .height = 540}})
        , m_camera(400.0F, {window().framebufferWidth(), window().framebufferHeight()})
    {
        // At least 480x360 world units stay visible, in wide and tall windows alike.
        m_camera.fitArea({480.0F, 360.0F});
    }

protected:
    void onUpdate(float deltaTime) override
    {
        m_time += deltaTime;
        const auto& in = input();
        if (in.isPressed(astra2d::Key::Escape)) {
            quit();
        }
        if (in.isPressed(astra2d::Key::Space)) {
            m_camera.setPosition({0.0F, 0.0F});
            m_camera.setZoom(1.0F);
        }
        // Panning: the world point under the mouse stays under the mouse. Converting both mouse
        // positions to world space handles zoom (and rotation) automatically.
        if (in.isHeld(astra2d::MouseButton::Left)) {
            const glm::vec2 now = m_camera.screenToWorld(in.mousePosition());
            const glm::vec2 before = m_camera.screenToWorld(in.mousePosition() - in.mouseDelta());
            m_camera.setPosition(m_camera.position() - (now - before));
        }
        if (in.scrollDelta().y != 0.0F) {
            const float zoom = m_camera.zoom() * std::pow(1.1F, in.scrollDelta().y);
            m_camera.setZoom(std::clamp(zoom, 0.2F, 5.0F));
        }
    }

    void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }

    void onRender() override
    {
        glClearColor(kBackground.r, kBackground.g, kBackground.b, kBackground.a);
        glClear(GL_COLOR_BUFFER_BIT);

        // Everything between begin() and end() is collected and drawn in as few draw calls as
        // possible (here: one).
        m_renderer.begin(m_camera);
        drawGrid();

        // Solid quads are given by centre, size and colour; the last argument is a rotation.
        m_renderer.drawQuad({-150.0F, 60.0F}, {80.0F, 80.0F}, {0.95F, 0.55F, 0.2F, 1.0F});
        m_renderer.drawQuad({0.0F, 60.0F}, {80.0F, 80.0F}, {0.3F, 0.6F, 1.0F, 1.0F}, m_time);
        // Colours with alpha < 1 are blended over what is behind them.
        m_renderer.drawQuad({150.0F, 60.0F}, {80.0F, 80.0F}, {0.8F, 0.3F, 0.9F, 0.5F});
        m_renderer.drawQuad({120.0F, 30.0F}, {80.0F, 80.0F}, {0.2F, 0.9F, 0.6F, 0.5F});

        // Outlines and lines, e.g. for debug drawing. Thickness is in world units.
        m_renderer.drawRect({-150.0F, -80.0F}, {90.0F, 60.0F}, {1.0F, 1.0F, 1.0F, 1.0F}, 2.0F);
        const float radius = 35.0F + (5.0F * std::sin(m_time * 2.0F));
        m_renderer.drawCircle({0.0F, -80.0F}, radius, {1.0F, 0.85F, 0.2F, 1.0F}, 3.0F, 48);
        drawStar({150.0F, -80.0F}, 45.0F);

        // A line from the origin to the mouse: screenToWorld() turns pixels into world units.
        const glm::vec2 mouse = m_camera.screenToWorld(input().mousePosition());
        m_renderer.drawLine({0.0F, 0.0F}, mouse, {1.0F, 1.0F, 1.0F, 0.5F}, 1.0F);
        m_renderer.end();
    }

private:
    // Grid lines every 50 world units over the visible area, plus the two axes.
    void drawGrid()
    {
        constexpr float kStep = 50.0F;
        const glm::vec2 min = m_camera.visibleMin();
        const glm::vec2 max = m_camera.visibleMax();
        // Integer line indices: a float loop counter would accumulate rounding errors.
        const auto first = [](float value) { return static_cast<int>(std::floor(value / kStep)); };
        const auto last = [](float value) { return static_cast<int>(std::ceil(value / kStep)); };
        for (int i = first(min.x); i <= last(max.x); ++i) {
            const float x = static_cast<float>(i) * kStep;
            m_renderer.drawLine({x, min.y}, {x, max.y}, kGrid);
        }
        for (int i = first(min.y); i <= last(max.y); ++i) {
            const float y = static_cast<float>(i) * kStep;
            m_renderer.drawLine({min.x, y}, {max.x, y}, kGrid);
        }
        m_renderer.drawLine({min.x, 0.0F}, {max.x, 0.0F}, kAxisX, 2.0F);
        m_renderer.drawLine({0.0F, min.y}, {0.0F, max.y}, kAxisY, 2.0F);
    }

    // A rotating five-pointed star made of lines.
    void drawStar(glm::vec2 center, float radius)
    {
        constexpr int kPoints = 5;
        const auto point = [&](int i) {
            const float angle = m_time + (static_cast<float>(i) * 2.0F * 2.0F *
                                          std::numbers::pi_v<float> / kPoints);
            return center + (radius * glm::vec2{std::cos(angle), std::sin(angle)});
        };
        for (int i = 0; i < kPoints; ++i) {
            m_renderer.drawLine(point(i), point(i + 1), {0.4F, 1.0F, 0.9F, 1.0F}, 2.0F);
        }
    }

    astra2d::OrthographicCamera m_camera;
    astra2d::Renderer2D m_renderer;
    float m_time = 0.0F;
};

} // namespace

int main()
{
    try {
        Shapes app;
        app.run();
    } catch (const std::exception& error) {
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
