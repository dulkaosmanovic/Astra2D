// sprites: textures, sprite sheets and frame animation.
//
// The sprite sheet examples/assets/textures/sprites.png holds 16x16 frames: a blob in row 0 and a
// spinning coin in row 1. An AnimationPlayer decides which frame to show; the renderer draws it.
// Movement runs in onFixedUpdate() (60 times per second) and is interpolated when drawing, so it
// is smooth at any frame rate.
//
// Controls:
//   A/D or Left/Right   walk (the sprite flips to face the direction)
//   Escape              quit

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/renderer/animation.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/renderer/sprite.hpp>

#include "../common/example_assets.hpp"

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <exception>

namespace {

constexpr float kScale = 4.0F;            // each texel drawn as 4x4 world units
constexpr float kSprite = 16.0F * kScale; // world size of one frame
constexpr float kWalkSpeed = 140.0F;      // world units per second
constexpr float kWorldHalfWidth = 300.0F; // the blob turns around here
constexpr glm::vec4 kTint{1.0F, 1.0F, 1.0F, 1.0F};

class Sprites : public astra2d::Application {
public:
    Sprites()
        : Application({.window = {.title = "Astra2D: sprites", .width = 960, .height = 540}})
        , m_assets(examples::assetsDirectory())
        // Pixel art: Nearest keeps the texels sharp when scaled up (the default).
        , m_sheet(m_assets.texture("textures/sprites.png"),
                  {examples::kSpriteFrame, examples::kSpriteFrame},
                  {examples::kSpriteMargin, examples::kSpriteMargin},
                  {examples::kSpriteSpacing, examples::kSpriteSpacing})
        , m_camera(540.0F, {window().framebufferWidth(), window().framebufferHeight()})
    {
        m_camera.fitArea({760.0F, 440.0F}); // the whole scene, whatever the window shape
    }

protected:
    void onStart() override
    {
        m_blob.play(m_idle);
        m_coin.play(m_spin);
    }

    void onFixedUpdate(float dt) override
    {
        float direction = 0.0F;
        if (input().isHeld(astra2d::Key::A) || input().isHeld(astra2d::Key::Left)) {
            direction -= 1.0F;
        }
        if (input().isHeld(astra2d::Key::D) || input().isHeld(astra2d::Key::Right)) {
            direction += 1.0F;
        }
        m_previousX = m_x;
        m_x = glm::clamp(m_x + (direction * kWalkSpeed * dt), -kWorldHalfWidth, kWorldHalfWidth);
        m_walking = direction != 0.0F;
        if (m_walking) {
            m_facingLeft = direction < 0.0F;
        }
    }

    void onUpdate(float dt) override
    {
        if (input().isPressed(astra2d::Key::Escape)) {
            quit();
        }
        // play() only restarts when the animation changes, so it can be called every frame.
        m_blob.play(m_walking ? m_walk : m_idle);
        m_blob.update(dt);
        m_coin.update(dt);
        m_time += dt;
    }

    void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }

    void onRender() override
    {
        glClearColor(0.10F, 0.11F, 0.18F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        m_renderer.begin(m_camera);
        // The ground: a plain quad.
        m_renderer.drawQuad({0.0F, -kSprite}, {2.0F * (kWorldHalfWidth + kSprite), kSprite},
                            {0.2F, 0.32F, 0.24F, 1.0F});

        // A row of coins at different points of the same animation (phase), bobbing up and down.
        for (int i = 0; i < 7; ++i) {
            const auto offset = static_cast<float>(i);
            const float bob = std::sin((m_time * 3.0F) + offset) * 6.0F;
            const int frame = 4 + ((m_coin.frameIndex() + i) % 4); // row 1 = frames 4..7
            m_renderer.drawSprite({(offset - 3.0F) * 90.0F, 150.0F + bob}, {kSprite, kSprite},
                                  m_sheet.frame(frame));
        }

        // The whole texture, to show what the sheet looks like.
        m_renderer.drawQuad({0.0F, -170.0F}, glm::vec2(m_sheet.texture().size()) * 2.0F,
                            m_sheet.texture());

        // The blob, interpolated between the last two fixed steps.
        const float x = glm::mix(m_previousX, m_x, interpolationAlpha());
        m_renderer.drawSprite({x, 0.0F}, {kSprite, kSprite}, m_sheet.frame(m_blob.frame()), kTint,
                              0.0F, m_facingLeft);
        m_renderer.end();
    }

private:
    astra2d::AssetCache m_assets;
    astra2d::SpriteSheet m_sheet;
    astra2d::OrthographicCamera m_camera;
    astra2d::Renderer2D m_renderer;

    // Animations are data; players refer to them, so they must live as long as the players.
    const astra2d::SpriteAnimation m_idle{.frames = {0, 1, 2, 1}, .frameDuration = 0.2F};
    const astra2d::SpriteAnimation m_walk{.frames = {0, 1, 2, 1}, .frameDuration = 0.08F};
    const astra2d::SpriteAnimation m_spin{.frames = {4, 5, 6, 7}, .frameDuration = 0.12F};
    astra2d::AnimationPlayer m_blob;
    astra2d::AnimationPlayer m_coin;

    float m_x = 0.0F;
    float m_previousX = 0.0F;
    bool m_walking = false;
    bool m_facingLeft = false;
    float m_time = 0.0F;
};

} // namespace

int main()
{
    try {
        Sprites app;
        app.run();
    } catch (const std::exception& error) {
        ASTRA_CRITICAL("{}", error.what()); // e.g. sprites.png not found
        return 1;
    }
    return 0;
}
