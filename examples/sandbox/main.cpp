// Sandbox: a small scene that exercises the core, renderer and scene modules. The blob, the coins,
// the ground and the spinning square are entities in a Scene; the parallax background, debug
// lines and HUD are drawn directly with Renderer2D around it.
//
// Controls (keyboard / gamepad):
//   WASD, arrows / left stick, d-pad   move the blob
//   Q, E / right stick                 rotate the camera
//   mouse wheel                        zoom
//   left click / X                     shake the camera (click also logs the world position)
//   P / Back                           CRT post-processing effect on/off
//   B                                  batching on/off (compare the draw calls in the HUD)
//   Space / Start                      reset the position
//   Escape                             quit
//
// While it runs, edit examples/assets/shaders/crt.frag: the effect is reloaded within half a
// second.

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/core/actions.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/events.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/debug/frame_stats.hpp>
#include <astra2d/renderer/animation.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/camera_shake.hpp>
#include <astra2d/renderer/font.hpp>
#include <astra2d/renderer/framebuffer.hpp>
#include <astra2d/renderer/post_process.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/renderer/sprite.hpp>
#include <astra2d/scene/components.hpp>
#include <astra2d/scene/scene.hpp>

#include "../common/example_assets.hpp"
#include "../common/hud.hpp"

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <string>

namespace {

constexpr float kTile = 32.0F;     // world units per sprite (16 texels drawn at 2x)
constexpr float kSpeed = 160.0F;   // world units per second
constexpr float kTexel = 2.0F;     // world units per background texel
constexpr float kTextSize = 16.0F; // HUD text: the 8 px font at 2x

constexpr glm::vec4 kWhite{1.0F};
constexpr glm::vec4 kSky{0.09F, 0.10F, 0.18F, 1.0F};
constexpr glm::vec4 kMountainFill{0.23F, 0.26F, 0.43F, 1.0F};
constexpr glm::vec4 kHillFill{0.13F, 0.29F, 0.24F, 1.0F};
constexpr glm::vec4 kGrass{0.20F, 0.32F, 0.24F, 1.0F};
constexpr glm::vec4 kGrassDark{0.17F, 0.27F, 0.21F, 1.0F};
constexpr glm::vec4 kDebugGreen{0.3F, 1.0F, 0.4F, 1.0F};
constexpr glm::vec4 kDebugYellow{1.0F, 0.85F, 0.2F, 0.8F};
constexpr glm::vec4 kHudText{0.95F, 0.95F, 0.85F, 1.0F};

constexpr astra2d::TextureSettings kPixelRepeat{.filter = astra2d::TextureFilter::Nearest,
                                                .wrap = astra2d::TextureWrap::Repeat};

// Components of the sandbox; the engine's Transform, SpriteRenderer, Animator and Interpolated do
// the rest.
struct Blob {
    bool moving = false;
    bool facingLeft = false;
};
struct Bob {
    float baseY = 0.0F;
    float phase = 0.0F;
};
struct Spin {
    float speed = 0.0F;
};

class Sandbox : public astra2d::Application {
public:
    Sandbox()
        : Application({.window = {.title = "Astra2D Sandbox", .width = 960, .height = 540}})
        , m_assets(examples::assetsDirectory())
        , m_sheet(m_assets.texture("textures/sprites.png"),
                  {examples::kSpriteFrame, examples::kSpriteFrame},
                  {examples::kSpriteMargin, examples::kSpriteMargin},
                  {examples::kSpriteSpacing, examples::kSpriteSpacing})
        , m_mountains(m_assets.texture("textures/mountains.png", kPixelRepeat))
        , m_hills(m_assets.texture("textures/hills.png", kPixelRepeat))
        , m_font(astra2d::Font::fromFile(m_assets.resolve("fonts/PressStart2P-Regular.ttf"), 8.0F))
        , m_camera(480.0F, framebufferSize())
        , m_sceneTarget(framebufferSize().x, framebufferSize().y)
    {
        bindActions();

        m_gamepadEvents = events().subscribe<astra2d::GamepadConnectionEvent>(
            [this](const astra2d::GamepadConnectionEvent& event) {
            m_notice = std::format("Gamepad {} {}", event.gamepad,
                                   event.connected ? "connected" : "disconnected");
            m_noticeTime = 3.0F;
        });
        m_focusEvents = events().subscribe<astra2d::WindowFocusEvent>(
            [](const astra2d::WindowFocusEvent& event) {
            ASTRA_INFO("Window {} focus", event.focused ? "gained" : "lost");
        });
    }

protected:
    void onStart() override
    {
        buildScene();
        // Load the effect up front, so a broken shader is reported at start-up.
        (void)m_assets.effect("shaders/crt.frag");
    }

    void onFixedUpdate(float dt) override { m_scene.fixedUpdate(dt); }

    void onUpdate(float dt) override
    {
        const auto& in = input();
        // One-shot actions (pressed/released) are read once per frame.
        if (m_actions.isPressed("quit", in)) {
            quit();
        }
        if (m_actions.isPressed("reset", in)) {
            m_scene.registry().get<astra2d::Transform>(m_blob).position = glm::vec2{0.0F};
            m_scene.resetInterpolation(m_blob); // jump there instead of sliding
            m_cameraRotation = 0.0F;
        }
        if (m_actions.isPressed("batching", in)) {
            m_renderer.setBatching(!m_renderer.batching());
        }
        if (m_actions.isPressed("postprocess", in)) {
            m_postProcessing = !m_postProcessing;
        }
        if (m_actions.isPressed("shake", in)) {
            m_shake.addTrauma(0.6F);
        }
        if (in.isPressed(astra2d::MouseButton::Left)) {
            const glm::vec2 world = m_camera.screenToWorld(in.mousePosition());
            ASTRA_INFO("Click at world ({:.1f}, {:.1f})", world.x, world.y);
        }
        if (in.scrollDelta().y != 0.0F) {
            const float zoom = m_camera.zoom() * std::pow(1.1F, in.scrollDelta().y);
            m_camera.setZoom(std::clamp(zoom, 0.25F, 4.0F));
        }
        m_cameraRotation += m_actions.axis("rotate", in) * dt;

        m_scene.update(dt); // bobbing coins, blob animation, then the built-in Animator system
        m_shake.update(dt);
        m_time += dt;
        m_noticeTime = std::max(m_noticeTime - dt, 0.0F);

        // Hot reload: pick up edited shaders and textures twice per second.
        m_reloadTimer += dt;
        if (m_reloadTimer >= 0.5F) {
            m_reloadTimer = 0.0F;
            m_assets.reloadChanged();
        }
        updateFps(dt);
    }

    void onResize(int width, int height) override
    {
        m_camera.setViewportSize(width, height);
        m_sceneTarget.resize(width, height);
    }

    void onRender() override
    {
        const glm::ivec2 size = framebufferSize();
        // With post-processing the scene goes into a texture first, otherwise straight to the
        // window.
        if (m_postProcessing) {
            m_sceneTarget.bind();
        } else {
            astra2d::Framebuffer::bindDefault(size.x, size.y);
        }
        glClearColor(kSky.r, kSky.g, kSky.b, kSky.a);
        glClear(GL_COLOR_BUFFER_BIT);
        m_renderer.resetStats();

        // Interpolate between the last two fixed steps for smooth motion at any frame rate.
        const float alpha = interpolationAlpha();
        const glm::vec2 player = m_scene.worldTransform(m_blob, alpha).position;
        m_shake.apply(m_camera, player, m_cameraRotation);

        drawBackgroundLayer(m_mountains, kMountainFill, {0.15F, 0.1F}, 70.0F);
        drawBackgroundLayer(m_hills, kHillFill, {0.45F, 0.3F}, 10.0F);
        drawWorld(player, alpha);

        if (m_postProcessing) {
            astra2d::Framebuffer::bindDefault(size.x, size.y);
            m_postProcess.apply(m_sceneTarget.colorTexture(), m_assets.effect("shaders/crt.frag"),
                                m_time);
        }
        drawHud(size); // after the effect, so the text stays sharp
    }

private:
    [[nodiscard]] glm::ivec2 framebufferSize() const
    {
        return {std::max(window().framebufferWidth(), 1),
                std::max(window().framebufferHeight(), 1)};
    }

    // The world as entities, and the systems that animate it.
    void buildScene()
    {
        auto& registry = m_scene.registry();
        using astra2d::SpriteRenderer;

        // A checkerboard island of solid quads around the origin.
        constexpr int kHalf = 4;
        for (int y = -kHalf; y < kHalf; ++y) {
            for (int x = -kHalf; x < kHalf; ++x) {
                const glm::vec2 center{(static_cast<float>(x) + 0.5F) * kTile,
                                       (static_cast<float>(y) + 0.5F) * kTile};
                const astra2d::Entity tile = m_scene.createEntity({}, center);
                registry.emplace<SpriteRenderer>(
                    tile, SpriteRenderer{.size = {kTile, kTile},
                                         .tint = ((x + y) % 2 == 0) ? kGrass : kGrassDark,
                                         .layer = -1});
            }
        }
        // Coins bobbing up and down, each a little out of step with the previous one.
        for (int i = 0; i < 5; ++i) {
            const astra2d::Entity coin =
                m_scene.createEntity("coin", {(static_cast<float>(i) - 2.0F) * 48.0F, 96.0F});
            registry.emplace<SpriteRenderer>(coin, SpriteRenderer{.size = {kTile, kTile}});
            registry.emplace<astra2d::Animator>(coin, astra2d::Animator{.sheet = &m_sheet})
                .player.play(m_coinSpin);
            registry.emplace<Bob>(coin, Bob{.baseY = 96.0F, .phase = static_cast<float>(i)});
        }
        const astra2d::Entity square = m_scene.createEntity("square", {-120.0F, 60.0F});
        registry.emplace<SpriteRenderer>(
            square, SpriteRenderer{.size = {40.0F, 40.0F}, .tint = {0.4F, 0.6F, 1.0F, 0.6F}});
        registry.emplace<Spin>(square, Spin{1.0F});

        m_blob = m_scene.createEntity("blob");
        registry.emplace<SpriteRenderer>(m_blob,
                                         SpriteRenderer{.size = {kTile, kTile}, .layer = 1});
        registry.emplace<astra2d::Animator>(m_blob, astra2d::Animator{.sheet = &m_sheet})
            .player.play(m_blobIdle);
        registry.emplace<Blob>(m_blob);
        registry.emplace<astra2d::Interpolated>(m_blob); // smooth at any frame rate

        using astra2d::SystemPhase;
        using astra2d::Transform;
        m_scene.addSystem(SystemPhase::FixedUpdate, "blob movement",
                          [this](astra2d::Scene& scene, float dt) {
            // Held state and axes are safe to read here; they do not depend on the frame/step
            // ratio.
            glm::vec2 direction{m_actions.axis("move_x", input()),
                                m_actions.axis("move_y", input())};
            if (glm::length(direction) > 1.0F) {
                direction = glm::normalize(direction); // diagonals are not faster
            }
            scene.registry().view<Blob, Transform>().each([&](Blob& blob, Transform& t) {
                t.position += direction * (kSpeed * dt);
                blob.moving = glm::length(direction) > 0.1F;
                if (direction.x != 0.0F) {
                    blob.facingLeft = direction.x < 0.0F;
                }
            });
        });
        m_scene.addSystem(SystemPhase::Update, "blob look", [this](astra2d::Scene& scene, float) {
            scene.registry().view<Blob, astra2d::Animator, SpriteRenderer>().each(
                [&](const Blob& blob, astra2d::Animator& animator, SpriteRenderer& sprite) {
                // Faster squish while walking.
                animator.player.play(blob.moving ? m_blobWalk : m_blobIdle);
                sprite.flipX = blob.facingLeft;
            });
        });
        m_scene.addSystem(SystemPhase::Update, "bob", [this](astra2d::Scene& scene, float) {
            scene.registry().view<Bob, Transform>().each([this](const Bob& bob, Transform& t) {
                t.position.y = bob.baseY + (std::sin((m_time * 3.0F) + bob.phase) * 3.0F);
            });
        });
        m_scene.addSystem(SystemPhase::Update, "spin", [](astra2d::Scene& scene, float dt) {
            scene.registry().view<Spin, Transform>().each(
                [dt](const Spin& spin, Transform& t) { t.rotation += spin.speed * dt; });
        });
    }

    void bindActions()
    {
        using astra2d::GamepadAxis;
        using astra2d::GamepadButton;
        using astra2d::Key;
        m_actions.bindAxis("move_x", Key::A, Key::D)
            .bindAxis("move_x", Key::Left, Key::Right)
            .bindAxis("move_x", GamepadButton::DpadLeft, GamepadButton::DpadRight)
            .bindAxis("move_x", GamepadAxis::LeftX);
        m_actions.bindAxis("move_y", Key::S, Key::W)
            .bindAxis("move_y", Key::Down, Key::Up)
            .bindAxis("move_y", GamepadButton::DpadDown, GamepadButton::DpadUp)
            .bindAxis("move_y", GamepadAxis::LeftY);
        m_actions.bindAxis("rotate", Key::E, Key::Q).bindAxis("rotate", GamepadAxis::RightX, -1.0F);
        m_actions.bind("shake", astra2d::MouseButton::Left).bind("shake", GamepadButton::X);
        m_actions.bind("postprocess", Key::P).bind("postprocess", GamepadButton::Back);
        m_actions.bind("batching", Key::B);
        m_actions.bind("reset", Key::Space).bind("reset", GamepadButton::Start);
        m_actions.bind("quit", Key::Escape);
    }

    // An endless strip of a tiling texture that scrolls slower than the world (parallax), filled
    // below with @p fill.
    void drawBackgroundLayer(const astra2d::Texture2D& texture, glm::vec4 fill, glm::vec2 parallax,
                             float y)
    {
        const astra2d::OrthographicCamera layer = m_camera.withParallax(parallax);
        const glm::vec2 min = layer.visibleMin();
        const glm::vec2 max = layer.visibleMax();
        const glm::vec2 tile = glm::vec2(texture.size()) * kTexel;
        const float width = max.x - min.x;
        const float centerX = (min.x + max.x) * 0.5F;

        m_renderer.begin(layer);
        const float bottom = y - (tile.y * 0.5F);
        // Anchored to the strip's bottom edge, so it shows exactly one tile vertically.
        m_renderer.drawTiled({centerX, y}, {width, tile.y}, texture, tile, kWhite, {0.0F, bottom});
        if (bottom > min.y) {
            m_renderer.drawQuad({centerX, (bottom + min.y) * 0.5F}, {width, bottom - min.y}, fill);
        }
        m_renderer.end();
    }

    void drawWorld(glm::vec2 player, float alpha)
    {
        m_renderer.begin(m_camera);
        // Ground, coins, the blob and the square: every SpriteRenderer, by layer, interpolated.
        m_scene.render(m_renderer, alpha);

        // Debug primitives: player bounds, a pickup radius and a line to the mouse.
        m_renderer.drawRect(player, {kTile, kTile}, kDebugGreen, 1.0F);
        m_renderer.drawCircle(player, kTile * 1.5F, kDebugYellow, 1.0F, 40);
        m_renderer.drawLine(player, m_camera.screenToWorld(input().mousePosition()), kDebugYellow,
                            1.0F);
        m_renderer.end();
    }

    void drawHud(glm::ivec2 size)
    {
        // Captured before the HUD adds its own quads and draw call.
        const astra2d::RenderStats scene = m_renderer.stats();
        const std::string lines = std::format(
            "{:.0f} FPS ({:.1f} ms)  {} draw calls  {} quads\n"
            "batching {} (B)  CRT {} (P)\n"
            "move WASD  rotate Q/E  zoom wheel\n"
            "click: shake  edit crt.frag live\n"
            "gamepad: {}{}{}",
            m_frameStats.report().fps, m_frameStats.report().averageMs, scene.drawCalls,
            scene.quads, m_renderer.batching() ? "on" : "off", m_postProcessing ? "on" : "off",
            input().isGamepadConnected(0) ? "connected" : "none", m_noticeTime > 0.0F ? "\n" : "",
            m_noticeTime > 0.0F ? m_notice : "");

        m_renderer.begin(astra2d::OrthographicCamera::screenSpace(size.x, size.y));
        examples::drawTextPanel(m_renderer, m_font, lines,
                                {24.0F, static_cast<float>(size.y) - 24.0F}, kTextSize, kHudText,
                                1.5F);
        m_renderer.end();
    }

    // The HUD's numbers change twice a second (FrameStats averages over that time).
    void updateFps(float dt) { m_frameStats.addFrame(dt); }

    astra2d::AssetCache m_assets;
    astra2d::SpriteSheet m_sheet;
    const astra2d::Texture2D& m_mountains;
    const astra2d::Texture2D& m_hills;
    astra2d::Font m_font;
    astra2d::OrthographicCamera m_camera;
    astra2d::CameraShake m_shake;
    astra2d::Framebuffer m_sceneTarget; // the scene before post-processing
    astra2d::Renderer2D m_renderer;
    astra2d::PostProcess m_postProcess;
    astra2d::ActionMap m_actions;
    astra2d::EventDispatcher::Subscription m_gamepadEvents;
    astra2d::EventDispatcher::Subscription m_focusEvents;

    const astra2d::SpriteAnimation m_blobIdle{.frames = {0, 1, 2, 1}, .frameDuration = 0.18F};
    const astra2d::SpriteAnimation m_blobWalk{.frames = {0, 1, 2, 1}, .frameDuration = 0.09F};
    const astra2d::SpriteAnimation m_coinSpin{.frames = {4, 5, 6, 7}, .frameDuration = 0.12F};
    // After the sheet and animations its entities point to, so it is destroyed before them.
    astra2d::Scene m_scene;
    astra2d::Entity m_blob;

    float m_cameraRotation = 0.0F;
    bool m_postProcessing = true;
    float m_time = 0.0F;
    float m_reloadTimer = 0.0F;

    std::string m_notice;
    float m_noticeTime = 0.0F;
    astra2d::FrameStats m_frameStats{0.5F};
};

} // namespace

int main()
{
    try {
        Sandbox sandbox;
        sandbox.run();
    } catch (const std::exception& error) {
        // E.g. a missing asset or no OpenGL 3.3 support: report it instead of std::terminate.
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
