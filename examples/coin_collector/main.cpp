// coin_collector: a small but complete game built from the core and renderer modules.
//
// Collect as many coins as possible in 30 seconds while avoiding the bouncing spikes; three hits
// and the round is over. It shows how the pieces fit together in a real game:
//   - game states (title, playing, game over) switched in onUpdate()
//   - gameplay in onFixedUpdate(), drawing interpolated in onRender()
//   - an ActionMap, so keyboard and gamepad both work
//   - events that decouple gameplay from its effects (score, camera shake, particles)
//   - a sprite sheet with animations, text for the HUD, screen shake
//
// Controls (keyboard / gamepad):
//   WASD, arrows / left stick, d-pad   move
//   Enter, Space / A, Start            start / restart
//   Escape                             quit

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/core/actions.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/events.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/renderer/animation.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/camera_shake.hpp>
#include <astra2d/renderer/font.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/renderer/sprite.hpp>

#include "../common/example_assets.hpp"

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------------------------------------
// Tuning

constexpr glm::vec2 kArenaHalf{300.0F, 180.0F}; // the playing field, centred on the origin
constexpr float kRoundTime = 30.0F;
constexpr int kLives = 3;
constexpr int kCoinsOnField = 5;
constexpr float kPlayerSpeed = 240.0F;
constexpr float kPlayerRadius = 14.0F;
constexpr float kCoinRadius = 12.0F;
constexpr float kSpikeRadius = 14.0F;
constexpr float kInvulnerableTime = 1.2F; // after a hit
constexpr float kSprite = 32.0F;          // 16 px frames drawn at 2x
constexpr float kTextSize = 16.0F;

constexpr glm::vec4 kBackground{0.06F, 0.07F, 0.11F, 1.0F};
constexpr glm::vec4 kArena{0.11F, 0.13F, 0.20F, 1.0F};
constexpr glm::vec4 kBorder{0.35F, 0.40F, 0.60F, 1.0F};
constexpr glm::vec4 kSpike{1.0F, 0.35F, 0.35F, 1.0F};
constexpr glm::vec4 kGold{1.0F, 0.85F, 0.3F, 1.0F};
constexpr glm::vec4 kWhite{1.0F};

// ---------------------------------------------------------------------------------------------
// Events: what happened in the game, published by the gameplay code. The effects subscribe.

struct CoinCollected {
    glm::vec2 position;
};

struct PlayerHit {
    glm::vec2 position;
    int livesLeft;
};

// ---------------------------------------------------------------------------------------------
// Game objects: plain data. (Module 4 of the engine, the ECS, will replace these.)

struct Spike {
    glm::vec2 position;
    glm::vec2 previous;
    glm::vec2 velocity;
};

struct Burst {
    glm::vec2 position;
    glm::vec4 color;
    float age = 0.0F;
};

enum class State { Title, Playing, GameOver };

bool overlaps(glm::vec2 a, float radiusA, glm::vec2 b, float radiusB)
{
    return glm::distance(a, b) < radiusA + radiusB;
}

class CoinCollector : public astra2d::Application {
public:
    CoinCollector()
        : Application({.window = {.title = "Coin Collector", .width = 960, .height = 600}})
        , m_assets(examples::assetsDirectory())
        , m_sheet(m_assets.texture("textures/sprites.png"),
                  {examples::kSpriteFrame, examples::kSpriteFrame},
                  {examples::kSpriteMargin, examples::kSpriteMargin},
                  {examples::kSpriteSpacing, examples::kSpriteSpacing})
        , m_font(astra2d::Font::fromFile(m_assets.resolve("fonts/PressStart2P-Regular.ttf"), 8.0F))
        , m_camera(2.0F * (kArenaHalf.y + 60.0F), framebufferSize())
        , m_shake({.maxOffset = {10.0F, 10.0F}, .maxAngle = 0.03F})
        , m_random(std::random_device{}())
    {
        // The arena plus the HUD above it stay fully visible in any window shape.
        m_camera.fitArea(2.0F * (kArenaHalf + glm::vec2{30.0F, 60.0F}));
        bindControls();

        // Each effect is its own subscriber; the gameplay code only publishes what happened.
        m_onCoin = events().subscribe<CoinCollected>([this](const CoinCollected& event) {
            ++m_score;
            m_shake.addTrauma(0.25F);
            m_bursts.push_back({.position = event.position, .color = kGold});
        });
        m_onHit = events().subscribe<PlayerHit>([this](const PlayerHit& event) {
            m_shake.addTrauma(0.7F);
            m_bursts.push_back({.position = event.position, .color = kSpike});
            ASTRA_INFO("Hit! {} lives left", event.livesLeft);
        });
    }

protected:
    void onStart() override
    {
        m_playerAnimation.play(m_idle);
        m_coinAnimation.play(m_spin);
    }

    void onFixedUpdate(float dt) override
    {
        if (m_state != State::Playing) {
            return;
        }
        updatePlayer(dt);
        updateSpikes(dt);
        collectCoins();
        checkHits();
    }

    void onUpdate(float dt) override
    {
        const auto& in = input();
        if (m_actions.isPressed("quit", in)) {
            quit();
        }
        switch (m_state) {
        case State::Title:
        case State::GameOver:
            if (m_actions.isPressed("start", in)) {
                startRound();
            }
            break;
        case State::Playing:
            m_timeLeft -= dt;
            m_invulnerable = std::max(m_invulnerable - dt, 0.0F);
            if (m_timeLeft <= 0.0F || m_lives <= 0) {
                endRound();
            }
            break;
        }

        m_playerAnimation.play(m_moving ? m_walk : m_idle);
        m_playerAnimation.update(dt);
        m_coinAnimation.update(dt);
        m_shake.update(dt);
        for (auto& burst : m_bursts) {
            burst.age += dt;
        }
        std::erase_if(m_bursts, [](const Burst& burst) { return burst.age > 0.5F; });
        m_time += dt;
    }

    void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }

    void onRender() override
    {
        glClearColor(kBackground.r, kBackground.g, kBackground.b, kBackground.a);
        glClear(GL_COLOR_BUFFER_BIT);

        m_shake.apply(m_camera, {0.0F, 0.0F});
        const float alpha = interpolationAlpha();

        m_renderer.begin(m_camera);
        m_renderer.drawQuad({0.0F, 0.0F}, 2.0F * kArenaHalf, kArena);
        m_renderer.drawRect({0.0F, 0.0F}, (2.0F * kArenaHalf) + glm::vec2{8.0F}, kBorder, 4.0F);

        for (const glm::vec2 coin : m_coins) {
            m_renderer.drawSprite(coin, {kSprite, kSprite}, m_sheet.frame(m_coinAnimation.frame()));
        }
        for (const Spike& spike : m_spikes) {
            const glm::vec2 position = glm::mix(spike.previous, spike.position, alpha);
            m_renderer.drawQuad(position, glm::vec2{kSpikeRadius * 1.6F}, kSpike, m_time * 4.0F);
            m_renderer.drawCircle(position, kSpikeRadius, kSpike, 2.0F, 16);
        }
        for (const Burst& burst : m_bursts) {
            const float t = burst.age / 0.5F;
            m_renderer.drawCircle(burst.position, 10.0F + (40.0F * t),
                                  {burst.color.r, burst.color.g, burst.color.b, 1.0F - t}, 3.0F);
        }

        // Blink while invulnerable.
        const bool visible = m_invulnerable <= 0.0F || std::fmod(m_time, 0.2F) < 0.1F;
        if (m_state != State::Title && visible) {
            const glm::vec2 player = glm::mix(m_previousPlayer, m_player, alpha);
            m_renderer.drawSprite(player, {kSprite, kSprite},
                                  m_sheet.frame(m_playerAnimation.frame()), kWhite, 0.0F,
                                  m_facingLeft);
        }
        drawHud();
        m_renderer.end();
    }

private:
    [[nodiscard]] glm::ivec2 framebufferSize() const
    {
        return {std::max(window().framebufferWidth(), 1),
                std::max(window().framebufferHeight(), 1)};
    }

    void bindControls()
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
        m_actions.bind("start", Key::Enter)
            .bind("start", Key::Space)
            .bind("start", GamepadButton::A)
            .bind("start", GamepadButton::Start);
        m_actions.bind("quit", Key::Escape);
    }

    // --- Round flow -------------------------------------------------------------------------

    void startRound()
    {
        m_state = State::Playing;
        m_score = 0;
        m_lives = kLives;
        m_timeLeft = kRoundTime;
        m_invulnerable = 0.0F;
        m_player = m_previousPlayer = {0.0F, 0.0F};
        m_coins.clear();
        for (int i = 0; i < kCoinsOnField; ++i) {
            m_coins.push_back(randomFreePosition());
        }
        m_spikes.clear();
        addSpike();
        addSpike();
    }

    void endRound()
    {
        m_state = State::GameOver;
        m_best = std::max(m_best, m_score);
        m_moving = false;
    }

    // --- Gameplay (fixed timestep) ----------------------------------------------------------

    void updatePlayer(float dt)
    {
        glm::vec2 direction{m_actions.axis("move_x", input()), m_actions.axis("move_y", input())};
        if (glm::length(direction) > 1.0F) {
            direction = glm::normalize(direction);
        }
        m_previousPlayer = m_player;
        const glm::vec2 limit = kArenaHalf - glm::vec2{kPlayerRadius};
        m_player = glm::clamp(m_player + (direction * kPlayerSpeed * dt), -limit, limit);
        m_moving = glm::length(direction) > 0.1F;
        if (direction.x != 0.0F) {
            m_facingLeft = direction.x < 0.0F;
        }
    }

    void updateSpikes(float dt)
    {
        const glm::vec2 limit = kArenaHalf - glm::vec2{kSpikeRadius};
        for (Spike& spike : m_spikes) {
            spike.previous = spike.position;
            spike.position += spike.velocity * dt;
            // Bounce off the walls.
            for (int axis = 0; axis < 2; ++axis) {
                if (std::abs(spike.position[axis]) > limit[axis]) {
                    spike.position[axis] = std::copysign(limit[axis], spike.position[axis]);
                    spike.velocity[axis] = -spike.velocity[axis];
                }
            }
        }
    }

    void collectCoins()
    {
        for (glm::vec2& coin : m_coins) {
            if (overlaps(m_player, kPlayerRadius, coin, kCoinRadius)) {
                events().publish(CoinCollected{.position = coin});
                coin = randomFreePosition();
                // Every fifth coin makes the game a little harder.
                if (m_score % 5 == 0) {
                    addSpike();
                }
            }
        }
    }

    void checkHits()
    {
        if (m_invulnerable > 0.0F) {
            return;
        }
        for (const Spike& spike : m_spikes) {
            if (overlaps(m_player, kPlayerRadius, spike.position, kSpikeRadius)) {
                --m_lives;
                m_invulnerable = kInvulnerableTime;
                events().publish(PlayerHit{.position = m_player, .livesLeft = m_lives});
                return;
            }
        }
    }

    void addSpike()
    {
        std::uniform_real_distribution<float> angle(0.0F, 6.2832F);
        std::uniform_real_distribution<float> speed(100.0F, 170.0F);
        const float a = angle(m_random);
        const glm::vec2 position = randomFreePosition();
        m_spikes.push_back({.position = position,
                            .previous = position,
                            .velocity = speed(m_random) * glm::vec2{std::cos(a), std::sin(a)}});
    }

    // A random point in the arena that is not right next to the player.
    glm::vec2 randomFreePosition()
    {
        const glm::vec2 limit = kArenaHalf - glm::vec2{24.0F};
        std::uniform_real_distribution<float> x(-limit.x, limit.x);
        std::uniform_real_distribution<float> y(-limit.y, limit.y);
        glm::vec2 position{};
        do {
            position = {x(m_random), y(m_random)};
        } while (glm::distance(position, m_player) < 100.0F);
        return position;
    }

    // --- HUD ----------------------------------------------------------------------------------

    void drawCentered(std::string_view text, float y, float size, glm::vec4 color)
    {
        const glm::vec2 extent = m_font.atlas().measure(text, size);
        m_renderer.drawText(m_font, text, {-extent.x * 0.5F, y + (extent.y * 0.5F)}, size, color);
    }

    void drawHud()
    {
        const float top = kArenaHalf.y + 40.0F;
        if (m_state != State::Title) {
            m_renderer.drawText(m_font, std::format("SCORE {}", m_score), {-kArenaHalf.x, top},
                                kTextSize, kGold);
            const std::string lives(static_cast<std::size_t>(std::max(m_lives, 0)), '*');
            drawCentered(std::format("LIVES {}", lives), top - 8.0F, kTextSize, kSpike);
            const std::string time =
                std::format("TIME {:2}", static_cast<int>(std::ceil(std::max(m_timeLeft, 0.0F))));
            const float width = m_font.atlas().measure(time, kTextSize).x;
            m_renderer.drawText(m_font, time, {kArenaHalf.x - width, top}, kTextSize, kWhite);
        }
        switch (m_state) {
        case State::Title:
            drawCentered("COIN COLLECTOR", 40.0F, 32.0F, kGold);
            drawCentered("collect coins, avoid spikes", -10.0F, kTextSize, kWhite);
            if (std::fmod(m_time, 1.0F) < 0.7F) {
                drawCentered("press ENTER or A", -60.0F, kTextSize, kWhite);
            }
            break;
        case State::GameOver:
            drawCentered(m_lives <= 0 ? "GAME OVER" : "TIME UP", 50.0F, 32.0F, kSpike);
            drawCentered(std::format("score {}   best {}", m_score, m_best), 0.0F, kTextSize,
                         kWhite);
            drawCentered("press ENTER to play again", -50.0F, kTextSize, kWhite);
            break;
        case State::Playing:
            break;
        }
    }

    // --- State --------------------------------------------------------------------------------

    astra2d::AssetCache m_assets;
    astra2d::SpriteSheet m_sheet;
    astra2d::Font m_font;
    astra2d::OrthographicCamera m_camera;
    astra2d::CameraShake m_shake;
    astra2d::Renderer2D m_renderer;
    astra2d::ActionMap m_actions;
    astra2d::EventDispatcher::Subscription m_onCoin;
    astra2d::EventDispatcher::Subscription m_onHit;
    std::mt19937 m_random;

    const astra2d::SpriteAnimation m_idle{.frames = {0, 1, 2, 1}, .frameDuration = 0.18F};
    const astra2d::SpriteAnimation m_walk{.frames = {0, 1, 2, 1}, .frameDuration = 0.08F};
    const astra2d::SpriteAnimation m_spin{.frames = {4, 5, 6, 7}, .frameDuration = 0.1F};
    astra2d::AnimationPlayer m_playerAnimation;
    astra2d::AnimationPlayer m_coinAnimation;

    State m_state = State::Title;
    glm::vec2 m_player{0.0F};
    glm::vec2 m_previousPlayer{0.0F};
    bool m_moving = false;
    bool m_facingLeft = false;
    std::vector<glm::vec2> m_coins;
    std::vector<Spike> m_spikes;
    std::vector<Burst> m_bursts;
    int m_score = 0;
    int m_best = 0;
    int m_lives = kLives;
    float m_timeLeft = kRoundTime;
    float m_invulnerable = 0.0F;
    float m_time = 0.0F;
};

} // namespace

int main()
{
    try {
        CoinCollector game;
        game.run();
    } catch (const std::exception& error) {
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
