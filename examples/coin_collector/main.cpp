// coin_collector: a small but complete game built on the engine's Scene and ECS.
//
// Collect as many coins as possible in 30 seconds while avoiding the bouncing spikes; three hits
// and the round is over. It shows how the pieces fit together in a real game:
//   - game objects are entities with components (Transform, SpriteRenderer, Animator, Collider and
//     the game's own Player, Coin, Spike, Burst); the gameplay is systems run by the Scene
//   - game states (title, playing, game over) switched in onUpdate()
//   - gameplay at the fixed timestep, drawn smoothly with Interpolated
//   - an ActionMap, so keyboard and gamepad both work
//   - events that decouple gameplay from its effects (score, camera shake, bursts)
//   - text for the HUD, screen shake
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
#include <astra2d/scene/components.hpp>
#include <astra2d/scene/scene.hpp>

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

using astra2d::Entity;
using astra2d::Scene;
using astra2d::SystemPhase;
using astra2d::Transform;

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
constexpr float kBurstTime = 0.5F;
constexpr float kSprite = 32.0F; // 16 px frames drawn at 2x
constexpr float kTextSize = 16.0F;

constexpr glm::vec4 kBackground{0.06F, 0.07F, 0.11F, 1.0F};
constexpr glm::vec4 kArena{0.11F, 0.13F, 0.20F, 1.0F};
constexpr glm::vec4 kBorder{0.35F, 0.40F, 0.60F, 1.0F};
constexpr glm::vec4 kSpikeColor{1.0F, 0.35F, 0.35F, 1.0F};
constexpr glm::vec4 kGold{1.0F, 0.85F, 0.3F, 1.0F};
constexpr glm::vec4 kWhite{1.0F};

// ---------------------------------------------------------------------------------------------
// Components of this game: plain data. The engine's Transform, SpriteRenderer, Animator, Collider
// and Interpolated do the rest.

struct Player {
    bool moving = false;
    bool facingLeft = false;
    float invulnerable = 0.0F; // seconds left
};

struct Coin {};

struct Spike {
    glm::vec2 velocity{0.0F};
};

struct Burst {
    glm::vec4 color{1.0F};
    float age = 0.0F;
};

// ---------------------------------------------------------------------------------------------
// Events: what happened in the game, published by the gameplay systems. The effects subscribe.

struct CoinCollected {
    glm::vec2 position;
};

struct PlayerHit {
    glm::vec2 position;
    int livesLeft;
};

enum class State { Title, Playing, GameOver };

astra2d::Collider circle(float radius)
{
    return {.shape = astra2d::ColliderShape::Circle, .radius = radius, .isTrigger = true};
}

// Circle-circle overlap of two entities with a Transform and a circle Collider.
bool overlaps(const astra2d::Registry& registry, Entity a, Entity b)
{
    const auto& colliderA = registry.get<astra2d::Collider>(a);
    const auto& colliderB = registry.get<astra2d::Collider>(b);
    const glm::vec2 centerA = registry.get<Transform>(a).position + colliderA.offset;
    const glm::vec2 centerB = registry.get<Transform>(b).position + colliderB.offset;
    return glm::distance(centerA, centerB) < colliderA.radius + colliderB.radius;
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
        addSystems();
        spawnArena(); // shown behind the title screen too

        // Each effect is its own subscriber; the gameplay systems only publish what happened.
        m_onCoin = events().subscribe<CoinCollected>([this](const CoinCollected& event) {
            ++m_score;
            m_shake.addTrauma(0.25F);
            spawnBurst(event.position, kGold);
        });
        m_onHit = events().subscribe<PlayerHit>([this](const PlayerHit& event) {
            m_shake.addTrauma(0.7F);
            spawnBurst(event.position, kSpikeColor);
            ASTRA_INFO("Hit! {} lives left", event.livesLeft);
        });
    }

protected:
    void onFixedUpdate(float dt) override
    {
        // The gameplay systems only run during a round; the title and game-over screens stand
        // still.
        if (m_state == State::Playing) {
            m_scene.fixedUpdate(dt);
        }
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
            if (m_timeLeft <= 0.0F || m_lives <= 0) {
                endRound();
            }
            break;
        }
        m_scene.update(dt); // animations and effects keep running between rounds
        m_shake.update(dt);
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
        // Every sprite in the scene, by layer, between the last two fixed steps.
        m_scene.render(m_renderer, alpha);
        // Outlines and rings are not sprites; they are drawn straight from the components.
        m_renderer.drawRect({0.0F, 0.0F}, (2.0F * kArenaHalf) + glm::vec2{8.0F}, kBorder, 4.0F);
        auto& registry = m_scene.registry();
        registry.view<Spike>().each([&](Entity spike, const Spike&) {
            m_renderer.drawCircle(m_scene.worldTransform(spike, alpha).position, kSpikeRadius,
                                  kSpikeColor, 2.0F, 16);
        });
        registry.view<Burst, Transform>().each([&](const Burst& burst, const Transform& t) {
            const float progress = burst.age / kBurstTime;
            m_renderer.drawCircle(t.position, 10.0F + (40.0F * progress),
                                  {burst.color.r, burst.color.g, burst.color.b, 1.0F - progress},
                                  3.0F);
        });
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

    // --- Systems: the gameplay, run by the Scene in this order ------------------------------

    void addSystems()
    {
        m_scene.addSystem(SystemPhase::FixedUpdate, "player movement",
                          [this](Scene& scene, float dt) { movePlayer(scene, dt); });
        m_scene.addSystem(SystemPhase::FixedUpdate, "spike movement", [](Scene& scene, float dt) {
            const glm::vec2 limit = kArenaHalf - glm::vec2{kSpikeRadius};
            scene.registry().view<Spike, Transform>().each([&](Spike& spike, Transform& t) {
                t.position += spike.velocity * dt;
                t.rotation += 4.0F * dt;
                for (int axis = 0; axis < 2; ++axis) { // bounce off the walls
                    if (std::abs(t.position[axis]) > limit[axis]) {
                        t.position[axis] = std::copysign(limit[axis], t.position[axis]);
                        spike.velocity[axis] = -spike.velocity[axis];
                    }
                }
            });
        });
        m_scene.addSystem(SystemPhase::FixedUpdate, "coin pickup",
                          [this](Scene& scene, float) { collectCoins(scene); });
        m_scene.addSystem(SystemPhase::FixedUpdate, "spike hits",
                          [this](Scene& scene, float) { checkHits(scene); });

        m_scene.addSystem(SystemPhase::Update, "player look", [this](Scene& scene, float dt) {
            scene.registry().view<Player, astra2d::Animator, astra2d::SpriteRenderer>().each(
                [&](Player& player, astra2d::Animator& animator, astra2d::SpriteRenderer& sprite) {
                player.invulnerable = std::max(player.invulnerable - dt, 0.0F);
                animator.player.play(player.moving ? m_walk : m_idle);
                sprite.flipX = player.facingLeft;
                // Blink while invulnerable.
                sprite.visible = player.invulnerable <= 0.0F || std::fmod(m_time, 0.2F) < 0.1F;
            });
        });
        m_scene.addSystem(SystemPhase::Update, "bursts", [](Scene& scene, float dt) {
            scene.registry().view<Burst>().each([&](Entity entity, Burst& burst) {
                burst.age += dt;
                if (burst.age > kBurstTime) {
                    scene.destroyLater(entity); // safe inside a view
                }
            });
        });
        // The built-in "animation" system (LateUpdate) then advances every Animator.
    }

    void movePlayer(Scene& scene, float dt)
    {
        glm::vec2 direction{m_actions.axis("move_x", input()), m_actions.axis("move_y", input())};
        if (glm::length(direction) > 1.0F) {
            direction = glm::normalize(direction);
        }
        scene.registry().view<Player, Transform>().each([&](Player& player, Transform& t) {
            const glm::vec2 limit = kArenaHalf - glm::vec2{kPlayerRadius};
            t.position = glm::clamp(t.position + (direction * kPlayerSpeed * dt), -limit, limit);
            player.moving = glm::length(direction) > 0.1F;
            if (direction.x != 0.0F) {
                player.facingLeft = direction.x < 0.0F;
            }
        });
    }

    void collectCoins(Scene& scene)
    {
        // First find the coins, then react. Reacting creates entities (a burst, maybe a spike),
        // and both get a Transform, a type this view iterates; doing that inside the loop could
        // move the Transform storage under the reference the loop holds.
        auto& registry = scene.registry();
        m_collected.clear();
        registry.view<Coin, Transform>().each([&](Entity coin, Coin&, Transform&) {
            if (overlaps(registry, m_player, coin)) {
                m_collected.push_back(coin);
            }
        });
        for (const Entity coin : m_collected) {
            const glm::vec2 position = registry.get<Transform>(coin).position; // a copy:
            events().publish(CoinCollected{.position = position}); // the handlers add entities
            registry.get<Transform>(coin).position = randomFreePosition(); // respawn elsewhere
            // Every fifth coin makes the game a little harder.
            if (m_score % 5 == 0) {
                spawnSpike();
            }
        }
    }

    void checkHits(Scene& scene)
    {
        auto& registry = scene.registry();
        auto& player = registry.get<Player>(m_player);
        if (player.invulnerable > 0.0F) {
            return;
        }
        for (const Entity spike : registry.view<Spike>()) {
            if (overlaps(registry, m_player, spike)) {
                --m_lives;
                player.invulnerable = kInvulnerableTime;
                const glm::vec2 position = registry.get<Transform>(m_player).position;
                events().publish(PlayerHit{.position = position, .livesLeft = m_lives});
                return;
            }
        }
    }

    // --- Creating entities -------------------------------------------------------------------

    void startRound()
    {
        m_state = State::Playing;
        m_score = 0;
        m_lives = kLives;
        m_timeLeft = kRoundTime;
        m_scene.clear();
        spawnArena();

        m_player = m_scene.createEntity("player");
        auto& registry = m_scene.registry();
        registry.emplace<Player>(m_player);
        registry.emplace<astra2d::SpriteRenderer>(
            m_player, astra2d::SpriteRenderer{.size = glm::vec2{kSprite}, .layer = 2});
        registry.emplace<astra2d::Animator>(m_player, astra2d::Animator{.sheet = &m_sheet})
            .player.play(m_idle);
        registry.emplace<astra2d::Collider>(m_player, circle(kPlayerRadius));
        registry.emplace<astra2d::Interpolated>(m_player);

        for (int i = 0; i < kCoinsOnField; ++i) {
            spawnCoin();
        }
        spawnSpike();
        spawnSpike();
    }

    void spawnArena()
    {
        const Entity arena = m_scene.createEntity("arena");
        m_scene.registry().emplace<astra2d::SpriteRenderer>(
            arena, astra2d::SpriteRenderer{.size = 2.0F * kArenaHalf, .tint = kArena, .layer = -1});
    }

    void endRound()
    {
        m_state = State::GameOver;
        m_best = std::max(m_best, m_score);
        m_scene.registry().get<Player>(m_player).moving = false;
    }

    void spawnCoin()
    {
        const Entity coin = m_scene.createEntity("coin", randomFreePosition());
        auto& registry = m_scene.registry();
        registry.emplace<Coin>(coin);
        registry.emplace<astra2d::SpriteRenderer>(
            coin, astra2d::SpriteRenderer{.size = glm::vec2{kSprite}, .layer = 1});
        registry.emplace<astra2d::Animator>(coin, astra2d::Animator{.sheet = &m_sheet})
            .player.play(m_spin);
        registry.emplace<astra2d::Collider>(coin, circle(kCoinRadius));
    }

    void spawnSpike()
    {
        std::uniform_real_distribution<float> angle(0.0F, 6.2832F);
        std::uniform_real_distribution<float> speed(100.0F, 170.0F);
        const float a = angle(m_random);
        const Entity spike = m_scene.createEntity("spike", randomFreePosition());
        auto& registry = m_scene.registry();
        registry.emplace<Spike>(spike,
                                Spike{speed(m_random) * glm::vec2{std::cos(a), std::sin(a)}});
        registry.emplace<astra2d::SpriteRenderer>(
            spike, astra2d::SpriteRenderer{
                       .size = glm::vec2{kSpikeRadius * 1.6F}, .tint = kSpikeColor, .layer = 1});
        registry.emplace<astra2d::Collider>(spike, circle(kSpikeRadius));
        registry.emplace<astra2d::Interpolated>(spike);
    }

    void spawnBurst(glm::vec2 position, glm::vec4 color)
    {
        const Entity burst = m_scene.createEntity({}, position);
        m_scene.registry().emplace<Burst>(burst, Burst{.color = color});
    }

    // A random point in the arena that is not right next to the player.
    glm::vec2 randomFreePosition()
    {
        const glm::vec2 player = m_scene.registry().valid(m_player)
                                     ? m_scene.registry().get<Transform>(m_player).position
                                     : glm::vec2{0.0F};
        const glm::vec2 limit = kArenaHalf - glm::vec2{24.0F};
        std::uniform_real_distribution<float> x(-limit.x, limit.x);
        std::uniform_real_distribution<float> y(-limit.y, limit.y);
        glm::vec2 position{};
        do {
            position = {x(m_random), y(m_random)};
        } while (glm::distance(position, player) < 100.0F);
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
            drawCentered(std::format("LIVES {}", lives), top - 8.0F, kTextSize, kSpikeColor);
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
            drawCentered(m_lives <= 0 ? "GAME OVER" : "TIME UP", 50.0F, 32.0F, kSpikeColor);
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
    std::mt19937 m_random;

    // Animations are data that Animators point to, so they are declared before the scene.
    const astra2d::SpriteAnimation m_idle{.frames = {0, 1, 2, 1}, .frameDuration = 0.18F};
    const astra2d::SpriteAnimation m_walk{.frames = {0, 1, 2, 1}, .frameDuration = 0.08F};
    const astra2d::SpriteAnimation m_spin{.frames = {4, 5, 6, 7}, .frameDuration = 0.1F};
    Scene m_scene;
    Entity m_player;
    std::vector<Entity> m_collected; // reused by collectCoins()
    astra2d::EventDispatcher::Subscription m_onCoin;
    astra2d::EventDispatcher::Subscription m_onHit;

    State m_state = State::Title;
    int m_score = 0;
    int m_best = 0;
    int m_lives = kLives;
    float m_timeLeft = kRoundTime;
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
