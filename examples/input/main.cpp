// input: action mapping, gamepads and events.
//
// The game asks for *actions* ("jump", "move_x") instead of keys, so keyboard and gamepad work at
// the same time and controls can be rebound while running. When the player jumps, the game
// publishes a JumpEvent; two independent parts of the program react to it (a particle ring and a
// counter) without knowing about each other or about the input code.
//
// Controls (keyboard / gamepad):
//   A/D, arrows / left stick, d-pad   move
//   Space / A button                  jump
//   R                                 rebind jump between Space and Enter
//   Escape                            quit
// A connected gamepad's sticks, triggers and buttons are shown on the right.

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/core/actions.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/events.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/font.hpp>
#include <astra2d/renderer/renderer2d.hpp>

#include "../common/example_assets.hpp"

#include <glad/gl.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <exception>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace {

// A game event: plain struct, no base class or registration.
struct JumpEvent {
    glm::vec2 position;
};

constexpr float kGravity = -1400.0F;
constexpr float kJumpSpeed = 560.0F;
constexpr float kMoveSpeed = 260.0F;
constexpr float kGround = -120.0F;
constexpr float kSize = 40.0F;

constexpr glm::vec4 kOn{1.0F, 0.85F, 0.25F, 1.0F};
constexpr glm::vec4 kOff{0.25F, 0.27F, 0.33F, 1.0F};
constexpr glm::vec4 kText{0.9F, 0.9F, 0.9F, 1.0F};

class InputDemo : public astra2d::Application {
public:
    InputDemo()
        : Application({.window = {.title = "Astra2D: input", .width = 960, .height = 540}})
        , m_assets(examples::assetsDirectory())
        , m_font(astra2d::Font::fromFile(m_assets.resolve("fonts/PressStart2P-Regular.ttf"), 8.0F))
        , m_camera(540.0F, {window().framebufferWidth(), window().framebufferHeight()})
    {
        m_camera.fitArea({920.0F, 540.0F}); // the demo and the gamepad view always fit

        using astra2d::GamepadAxis;
        using astra2d::GamepadButton;
        using astra2d::Key;
        m_actions.bindAxis("move_x", Key::A, Key::D)
            .bindAxis("move_x", Key::Left, Key::Right)
            .bindAxis("move_x", GamepadButton::DpadLeft, GamepadButton::DpadRight)
            .bindAxis("move_x", GamepadAxis::LeftX);
        m_actions.bind("jump", Key::Space).bind("jump", GamepadButton::A);
        m_actions.bind("rebind", Key::R);
        m_actions.bind("quit", Key::Escape);

        // Two listeners of the same event. Each Subscription keeps its handler registered for as
        // long as it lives (here: as long as the demo).
        m_ringEffect = events().subscribe<JumpEvent>([this](const JumpEvent& event) {
            m_rings.push_back({.position = event.position, .age = 0.0F});
        });
        m_jumpCounter =
            events().subscribe<JumpEvent>([this](const JumpEvent& /*event*/) { ++m_jumps; });

        // Engine events: the window and gamepads publish these.
        m_gamepadEvents = events().subscribe<astra2d::GamepadConnectionEvent>(
            [](const astra2d::GamepadConnectionEvent& event) {
            ASTRA_INFO("Gamepad {} {}", event.gamepad,
                       event.connected ? "connected" : "disconnected");
        });
    }

protected:
    void onFixedUpdate(float dt) override
    {
        m_velocity.x = m_actions.axis("move_x", input()) * kMoveSpeed;
        m_velocity.y += kGravity * dt;
        m_position += m_velocity * dt;
        if (m_position.y <= kGround) {
            m_position.y = kGround;
            m_velocity.y = 0.0F;
        }
        m_position.x = std::clamp(m_position.x, -420.0F, 120.0F);
    }

    void onUpdate(float dt) override
    {
        const auto& in = input();
        if (m_actions.isPressed("quit", in)) {
            quit();
        }
        // isPressed() is true for exactly one frame, so it is read in onUpdate(), not in
        // onFixedUpdate() (which can run 0 or several times per frame).
        if (m_actions.isPressed("jump", in) && m_position.y <= kGround) {
            m_velocity.y = kJumpSpeed;
            events().publish(JumpEvent{.position = m_position});
        }
        if (m_actions.isPressed("rebind", in)) {
            m_jumpOnEnter = !m_jumpOnEnter;
            m_actions.unbind("jump");
            m_actions.bind("jump", m_jumpOnEnter ? astra2d::Key::Enter : astra2d::Key::Space)
                .bind("jump", astra2d::GamepadButton::A);
        }
        for (auto& ring : m_rings) {
            ring.age += dt;
        }
        std::erase_if(m_rings, [](const Ring& ring) { return ring.age > 0.6F; });
    }

    void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }

    void onRender() override
    {
        glClearColor(0.08F, 0.09F, 0.12F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        m_renderer.begin(m_camera);
        m_renderer.drawQuad({-150.0F, kGround - (kSize * 0.5F) - 10.0F}, {600.0F, 20.0F}, kOff);
        for (const auto& ring : m_rings) {
            const float t = ring.age / 0.6F;
            m_renderer.drawCircle(ring.position, 20.0F + (80.0F * t),
                                  {1.0F, 0.85F, 0.25F, 1.0F - t}, 3.0F, 40);
        }
        const bool jumpHeld = m_actions.isHeld("jump", input());
        m_renderer.drawQuad(m_position, {kSize, kSize},
                            jumpHeld ? kOn : glm::vec4{0.3F, 0.7F, 1.0F, 1.0F});
        drawGamepad({320.0F, 0.0F});

        const std::string info =
            std::format("move_x {:+.2f}\njump: {} (R rebinds)\njumps: {}\ngamepad: {}",
                        m_actions.axis("move_x", input()), m_jumpOnEnter ? "Enter" : "Space",
                        m_jumps, input().isGamepadConnected(0) ? "connected" : "none");
        m_renderer.drawText(m_font, info, {-440.0F, 230.0F}, 16.0F, kText, 1.6F);
        m_renderer.end();
    }

private:
    struct Ring {
        glm::vec2 position;
        float age;
    };

    // The raw state of gamepad 0: sticks (after the dead zone), triggers and the face buttons.
    void drawGamepad(glm::vec2 center)
    {
        using astra2d::GamepadAxis;
        using astra2d::GamepadButton;
        const auto& in = input();
        constexpr float kStick = 40.0F;
        const std::array sticks{std::pair{center + glm::vec2{-60.0F, 40.0F}, in.leftStick()},
                                std::pair{center + glm::vec2{60.0F, 40.0F}, in.rightStick()}};
        for (const auto& [base, value] : sticks) {
            m_renderer.drawCircle(base, kStick, kOff, 2.0F);
            m_renderer.drawQuad(base + (value * kStick), {12.0F, 12.0F}, kOn);
        }
        // Triggers go from 0 (released) to 1 (fully pressed).
        const std::array triggers{std::pair{-60.0F, in.axis(GamepadAxis::LeftTrigger)},
                                  std::pair{60.0F, in.axis(GamepadAxis::RightTrigger)}};
        for (const auto& [x, value] : triggers) {
            m_renderer.drawRect(center + glm::vec2{x, 120.0F}, {60.0F, 14.0F}, kOff, 2.0F);
            m_renderer.drawQuad(center + glm::vec2{x - 30.0F + (30.0F * value), 120.0F},
                                {60.0F * value, 14.0F}, kOn);
        }
        const std::array buttons{std::pair{GamepadButton::A, glm::vec2{0.0F, -80.0F}},
                                 std::pair{GamepadButton::B, glm::vec2{24.0F, -56.0F}},
                                 std::pair{GamepadButton::X, glm::vec2{-24.0F, -56.0F}},
                                 std::pair{GamepadButton::Y, glm::vec2{0.0F, -32.0F}}};
        for (const auto& [button, offset] : buttons) {
            m_renderer.drawQuad(center + offset, {18.0F, 18.0F}, in.isHeld(button) ? kOn : kOff);
        }
        m_renderer.drawText(m_font, "gamepad 0", center + glm::vec2{-72.0F, -120.0F}, 16.0F, kText);
    }

    astra2d::AssetCache m_assets;
    astra2d::Font m_font;
    astra2d::OrthographicCamera m_camera;
    astra2d::Renderer2D m_renderer;
    astra2d::ActionMap m_actions;
    astra2d::EventDispatcher::Subscription m_ringEffect;
    astra2d::EventDispatcher::Subscription m_jumpCounter;
    astra2d::EventDispatcher::Subscription m_gamepadEvents;

    glm::vec2 m_position{-150.0F, kGround};
    glm::vec2 m_velocity{0.0F};
    std::vector<Ring> m_rings;
    int m_jumps = 0;
    bool m_jumpOnEnter = false;
};

} // namespace

int main()
{
    try {
        InputDemo app;
        app.run();
    } catch (const std::exception& error) {
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
