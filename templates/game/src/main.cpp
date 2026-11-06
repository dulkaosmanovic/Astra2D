// A starting point for a game made with Astra2D: a character that walks around a small level.
// Rename it, replace the art and build from here.
//
// Controls: WASD / arrows / left stick move, Escape quits.

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/core/actions.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/core/paths.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/renderer2d.hpp>

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <exception>
#include <filesystem>

namespace {

constexpr float kSpeed = 150.0F;
constexpr glm::vec2 kLevelHalf{200.0F, 120.0F};

// Installed: the assets directory next to the executable (meson.build installs it there).
// Development: the source tree's assets/, so edits show up without reinstalling.
std::filesystem::path findAssets()
{
    return astra2d::findDirectory(
        {astra2d::executableDirectory() / "assets", GAME_SOURCE_ASSETS_DIR});
}

class MyGame : public astra2d::Application {
public:
    MyGame()
        : Application({.window = {.title = "My Game", .width = 1280, .height = 720}})
        , m_assets(findAssets())
        , m_player(m_assets.texture("textures/player.png"))
        , m_camera(360.0F, {window().framebufferWidth(), window().framebufferHeight()})
    {
        ASTRA_INFO("Assets from {}", m_assets.root().string());
        m_camera.fitArea(2.0F * kLevelHalf + glm::vec2{40.0F}); // the whole level, any window

        using astra2d::GamepadAxis;
        using astra2d::Key;
        m_actions.bindAxis("move_x", Key::A, Key::D)
            .bindAxis("move_x", Key::Left, Key::Right)
            .bindAxis("move_x", GamepadAxis::LeftX);
        m_actions.bindAxis("move_y", Key::S, Key::W)
            .bindAxis("move_y", Key::Down, Key::Up)
            .bindAxis("move_y", GamepadAxis::LeftY);
        m_actions.bind("quit", Key::Escape);
    }

protected:
    // Game logic at a fixed 60 steps per second.
    void onFixedUpdate(float dt) override
    {
        glm::vec2 direction{m_actions.axis("move_x", input()), m_actions.axis("move_y", input())};
        if (glm::length(direction) > 1.0F) {
            direction = glm::normalize(direction);
        }
        m_previous = m_position;
        m_position = glm::clamp(m_position + (direction * kSpeed * dt), -kLevelHalf, kLevelHalf);
    }

    // Once per frame: one-shot input, animation, UI.
    void onUpdate(float /*dt*/) override
    {
        if (m_actions.isPressed("quit", input())) {
            quit();
        }
    }

    void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }

    void onRender() override
    {
        glClearColor(0.05F, 0.06F, 0.09F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        m_renderer.begin(m_camera);
        m_renderer.drawQuad({0.0F, 0.0F}, 2.0F * kLevelHalf + glm::vec2{32.0F},
                            {0.13F, 0.16F, 0.24F, 1.0F});
        // Interpolated between the last two fixed steps, so motion is smooth at any frame rate.
        const glm::vec2 player = glm::mix(m_previous, m_position, interpolationAlpha());
        m_renderer.drawQuad(player, {32.0F, 32.0F}, m_player);
        m_renderer.end();
    }

private:
    astra2d::AssetCache m_assets;
    const astra2d::Texture2D& m_player;
    astra2d::OrthographicCamera m_camera;
    astra2d::Renderer2D m_renderer;
    astra2d::ActionMap m_actions;
    glm::vec2 m_position{0.0F};
    glm::vec2 m_previous{0.0F};
};

} // namespace

int main()
{
    try {
        MyGame game;
        game.run();
    } catch (const std::exception& error) {
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
