// coin_collector: a small but complete game built on the engine's Scene and ECS.
//
// Collect as many coins as possible in 30 seconds while avoiding the bouncing spikes; three hits
// and the round is over. It shows how the pieces fit together in a real game:
//   - game objects are entities with components (Transform, SpriteRenderer, Animator, RigidBody,
//     Collider and the game's own Player, Coin, Spike, Burst); the gameplay is systems run by the
//     Scene
//   - the PhysicsSystem moves the player and the spikes (which bounce off the walls by
//     themselves) and reports coins as trigger overlaps and spike hits as collisions
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
//   F1                                 debug overlay (frame graph, profiler, entities)
//   F3                                 physics debug view
//   F2                                 log FPS, frame times and render statistics

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/core/actions.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/events.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/debug/debug_overlay.hpp>
#include <astra2d/debug/frame_stats.hpp>
#include <astra2d/debug/physics_debug_draw.hpp>
#include <astra2d/renderer/animation.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/camera_shake.hpp>
#include <astra2d/renderer/font.hpp>
#include <astra2d/renderer/particles.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/renderer/sprite.hpp>
#include <astra2d/scene/components.hpp>
#include <astra2d/scene/physics_system.hpp>
#include <astra2d/scene/scene.hpp>

#include "../common/example_assets.hpp"
#include "../common/hud.hpp"

#include <glad/gl.h>
#include <imgui.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

struct Spike {};

// A ring that grows and fades; its Lifetime destroys it.
struct Burst {
    glm::vec4 color{1.0F};
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

// Collision groups (Collider::category / mask): what collides with what.
constexpr std::uint16_t kWallGroup = 1U << 0U;
constexpr std::uint16_t kPlayerGroup = 1U << 1U;
constexpr std::uint16_t kSpikeGroup = 1U << 2U;
constexpr std::uint16_t kCoinGroup = 1U << 3U;

astra2d::Collider circle(float radius, std::uint16_t category, std::uint16_t mask)
{
    // No friction and fully elastic: spikes keep their speed when they bounce.
    return {.shape = astra2d::ColliderShape::Circle,
            .radius = radius,
            .restitution = 1.0F,
            .friction = 0.0F,
            .category = category,
            .mask = mask};
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
        // Measured in pixels, with 32 pixel sprites: this scales the physics' length tolerances.
        , m_physics(m_scene, {.gravity = {0.0F, 0.0F}, .lengthUnitsPerMeter = 32.0F})
    {
        // The arena plus the HUD above it stay fully visible in any window shape.
        m_camera.fitArea(2.0F * (kArenaHalf + glm::vec2{30.0F, 60.0F}));
        bindControls();
        addSystems();
        // Gameplay that reacts to the physics: the handlers run after each physics step.
        m_onTrigger = m_physics.onTriggerEnter().connect(
            [this](const astra2d::TriggerContact& contact) { collectCoin(contact.trigger); });
        m_onCollision = m_physics.onCollisionBegin().connect(
            [this](const astra2d::CollisionEvent& event) { spikeHit(event); });
        spawnArena(); // shown behind the title screen too

        // Each effect is its own subscriber; the gameplay systems only publish what happened.
        m_onCoin = events().subscribe<CoinCollected>([this](const CoinCollected& event) {
            ++m_score;
            m_shake.addTrauma(0.25F);
            spawnBurst(event.position, kGold);
            m_sparks.settings().startColor = kGold;
            m_sparks.emit(event.position, 24);
        });
        m_onHit = events().subscribe<PlayerHit>([this](const PlayerHit& event) {
            m_shake.addTrauma(0.7F);
            spawnBurst(event.position, kSpikeColor);
            m_sparks.settings().startColor = kSpikeColor;
            m_sparks.emit(event.position, 40);
            ASTRA_INFO("Hit! {} lives left", event.livesLeft);
        });

        // F1: the engine's overlay, with this game's switches and components.
        m_overlay.setScene(&m_scene);
        m_overlay.setCamera(&m_camera); // "Pick": click an entity to inspect it
        m_overlay.addToggle("Physics debug view (F3)", &m_showPhysics);
        m_overlay.addToggle("Frame statistics in the log (F2)", &m_logStats);
        m_overlay.addPanel("Round", [this] {
            ImGui::SliderInt("Lives", &m_lives, 1, kLives);
            ImGui::DragFloat("Time left", &m_timeLeft, 0.1F, 0.0F, kRoundTime);
        });
        m_overlay.inspector().addInspector<Player>("Player", [](Player& player) {
            ImGui::DragFloat("Invulnerable (s)", &player.invulnerable, 0.01F, 0.0F, 10.0F);
        });
        m_overlay.inspector().addInspector<Burst>(
            "Burst", [](Burst& burst) { ImGui::ColorEdit4("Colour", &burst.color.r); });
        const auto tag = [](auto&) { ImGui::TextUnformatted("(a tag without data)"); };
        m_overlay.inspector().addInspector<Coin>("Coin", tag);
        m_overlay.inspector().addInspector<Spike>("Spike", tag);
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
        if (m_actions.isPressed("debug", in)) {
            m_showPhysics = !m_showPhysics;
        }
        if (m_actions.isPressed("overlay", in)) {
            m_overlay.toggle();
        }
        if (m_actions.isPressed("stats", in)) {
            m_logStats = !m_logStats;
            m_frameStats.restart();
            ASTRA_INFO("Frame statistics in the log: {}", m_logStats ? "on" : "off");
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
        m_sparks.update(dt);
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
        registry.view<Burst, astra2d::Lifetime, Transform>().each(
            [&](const Burst& burst, const astra2d::Lifetime& lifetime, const Transform& t) {
            const float progress = lifetime.progress();
            m_renderer.drawCircle(t.position, 10.0F + (40.0F * progress),
                                  {burst.color.r, burst.color.g, burst.color.b, 1.0F - progress},
                                  3.0F);
        });
        m_sparks.draw(m_renderer);
        if (m_showPhysics) {
            astra2d::drawPhysicsDebug(m_renderer, m_physics.world(),
                                      {.lineWidth = 1.5F,
                                       .contactSize = 6.0F,
                                       .normalLength = 16.0F,
                                       .velocityScale = 0.15F});
        }
        m_overlay.drawSelection(m_renderer, 2.0F); // the entity picked in the F1 overlay
        drawHud();
        m_renderer.end();
        m_overlay.addFrame(m_renderer.stats());
        if (m_logStats && m_frameStats.addFrame(frameTime(), m_renderer.stats())) {
            ASTRA_INFO("{}", m_frameStats.summary());
        }
        m_renderer.resetStats();
        m_overlay.draw();
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
        m_actions.bind("debug", Key::F3);
        m_actions.bind("stats", Key::F2);
        m_actions.bind("overlay", Key::F1);
    }

    // --- Systems: the gameplay, run by the Scene in this order ------------------------------

    void addSystems()
    {
        // The "physics" system was added when m_physics was made, so it runs first: it moves the
        // bodies with the velocities set in the step before (one step, 1/60 s, of input delay).
        m_scene.addSystem(SystemPhase::FixedUpdate, "player movement",
                          [this](Scene& scene, float) { movePlayer(scene); });

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
        // The built-in "animation" system (LateUpdate) then advances every Animator, and the
        // "lifetime" system removes bursts whose time is up.
    }

    void movePlayer(Scene& scene)
    {
        glm::vec2 direction{m_actions.axis("move_x", input()), m_actions.axis("move_y", input())};
        if (glm::length(direction) > 1.0F) {
            direction = glm::normalize(direction);
        }
        // Setting the velocity is enough: the physics moves the player and the walls stop it.
        scene.registry().view<Player, astra2d::RigidBody>().each(
            [&](Player& player, astra2d::RigidBody& body) {
            body.velocity = direction * kPlayerSpeed;
            player.moving = glm::length(direction) > 0.1F;
            if (direction.x != 0.0F) {
                player.facingLeft = direction.x < 0.0F;
            }
        });
    }

    // A coin's trigger was entered (only the player can: coins are masked to the player group).
    void collectCoin(Entity coin)
    {
        auto& registry = m_scene.registry();
        if (!registry.has<Coin>(coin)) {
            return;
        }
        // The handlers of the event count the score and create entities (safe here: outside any
        // view).
        events().publish(CoinCollected{.position = registry.get<Transform>(coin).position});
        // Moving a static collider's Transform moves its body at the next physics step.
        registry.get<Transform>(coin).position = randomFreePosition();
        // Every fifth coin makes the game a little harder.
        if (m_score % 5 == 0) {
            spawnSpike();
        }
    }

    void spikeHit(const astra2d::CollisionEvent& event)
    {
        if (event.a != m_player && event.b != m_player) {
            return; // e.g. two spikes
        }
        auto& registry = m_scene.registry();
        auto& player = registry.get<Player>(m_player);
        if (!registry.has<Spike>(event.other(m_player)) || player.invulnerable > 0.0F) {
            return;
        }
        --m_lives;
        player.invulnerable = kInvulnerableTime;
        events().publish(PlayerHit{.position = registry.get<Transform>(m_player).position,
                                   .livesLeft = m_lives});
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
        // A dynamic body without gravity that never turns; spikes bounce off it.
        registry.emplace<astra2d::RigidBody>(m_player, astra2d::RigidBody{.fixedRotation = true});
        registry.emplace<astra2d::Collider>(
            m_player, circle(kPlayerRadius, kPlayerGroup,
                             astra2d::collisionGroups(kWallGroup, kSpikeGroup, kCoinGroup)));
        registry.emplace<astra2d::Interpolated>(m_player);

        for (int i = 0; i < kCoinsOnField; ++i) {
            spawnCoin();
        }
        spawnSpike();
        spawnSpike();
    }

    void spawnArena()
    {
        auto& registry = m_scene.registry();
        const Entity arena = m_scene.createEntity("arena");
        registry.emplace<astra2d::SpriteRenderer>(
            arena, astra2d::SpriteRenderer{.size = 2.0F * kArenaHalf, .tint = kArena, .layer = -1});
        // Four walls just outside the field: colliders without a RigidBody are static.
        constexpr float kThickness = 20.0F;
        for (const glm::vec2 side :
             {glm::vec2(1, 0), glm::vec2(-1, 0), glm::vec2(0, 1), glm::vec2(0, -1)}) {
            const glm::vec2 center = side * (kArenaHalf + glm::vec2(kThickness));
            const glm::vec2 half = side.x != 0.0F
                                       ? glm::vec2(kThickness, kArenaHalf.y + (2.0F * kThickness))
                                       : glm::vec2(kArenaHalf.x + (2.0F * kThickness), kThickness);
            const Entity wall = m_scene.createEntity("wall", center);
            registry.emplace<astra2d::Collider>(wall, astra2d::Collider{.halfExtents = half,
                                                                        .restitution = 1.0F,
                                                                        .friction = 0.0F,
                                                                        .category = kWallGroup});
        }
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
        // A trigger: reports the player entering it, but nothing bounces off it.
        astra2d::Collider trigger = circle(kCoinRadius, kCoinGroup, kPlayerGroup);
        trigger.isTrigger = true;
        registry.emplace<astra2d::Collider>(coin, trigger);
    }

    void spawnSpike()
    {
        std::uniform_real_distribution<float> angle(0.0F, 6.2832F);
        std::uniform_real_distribution<float> speed(100.0F, 170.0F);
        const float a = angle(m_random);
        const Entity spike = m_scene.createEntity("spike", randomFreePosition());
        auto& registry = m_scene.registry();
        registry.emplace<Spike>(spike);
        // Thrown in a random direction, spinning; walls, other spikes and the player bounce it.
        registry.emplace<astra2d::RigidBody>(
            spike,
            astra2d::RigidBody{.velocity = speed(m_random) * glm::vec2{std::cos(a), std::sin(a)},
                               .angularVelocity = 4.0F,
                               .allowSleep = false});
        registry.emplace<astra2d::SpriteRenderer>(
            spike, astra2d::SpriteRenderer{
                       .size = glm::vec2{kSpikeRadius * 1.6F}, .tint = kSpikeColor, .layer = 1});
        registry.emplace<astra2d::Collider>(
            spike, circle(kSpikeRadius, kSpikeGroup,
                          astra2d::collisionGroups(kWallGroup, kSpikeGroup, kPlayerGroup)));
        registry.emplace<astra2d::Interpolated>(spike);
    }

    void spawnBurst(glm::vec2 position, glm::vec4 color)
    {
        const Entity burst = m_scene.createEntity({}, position);
        m_scene.registry().emplace<Burst>(burst, Burst{.color = color});
        m_scene.registry().emplace<astra2d::Lifetime>(burst,
                                                      astra2d::Lifetime{.duration = kBurstTime});
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

    // Text centred on (0, y).
    void drawCentered(std::string_view text, float y, float size, glm::vec4 color)
    {
        m_renderer.drawText(m_font, text, {0.0F, y},
                            {.size = size, .color = color, .anchor = {0.5F, 0.5F}});
    }

    void drawHud()
    {
        // A bar above the arena: score on the left, a slime per life in the middle, time on the
        // right.
        const float bottom = kArenaHalf.y + 12.0F;
        const float middle = bottom + 18.0F;
        const float textTop = middle + (kTextSize * 0.5F);
        if (m_state != State::Title) {
            examples::drawPanel(m_renderer, {-kArenaHalf.x - 4.0F, bottom},
                                {kArenaHalf.x + 4.0F, bottom + 36.0F});
            m_renderer.drawText(m_font, std::format("SCORE {}", m_score),
                                {-kArenaHalf.x + 12.0F, textTop}, kTextSize, kGold);
            for (int i = 0; i < kLives; ++i) {
                const float x = (static_cast<float>(i) - ((kLives - 1) * 0.5F)) * 28.0F;
                m_renderer.drawSprite({x, middle}, glm::vec2{24.0F}, m_sheet.frame(0),
                                      {1.0F, 1.0F, 1.0F, i < m_lives ? 1.0F : 0.2F});
            }
            const std::string time =
                std::format("TIME {:2}", static_cast<int>(std::ceil(std::max(m_timeLeft, 0.0F))));
            m_renderer.drawText(m_font, time, {kArenaHalf.x - 12.0F, textTop},
                                {.size = kTextSize,
                                 .color = m_timeLeft < 5.0F ? kSpikeColor : kWhite,
                                 .anchor = {1.0F, 1.0F}}); // right-aligned
        }
        if (m_state != State::Playing) {
            examples::drawPanel(m_renderer, {-260.0F, -100.0F}, {260.0F, 100.0F}, 4.0F, kBorder);
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
    // Sparks thrown out of a collected coin or a hit: fast at first, slowed by drag, fading.
    astra2d::ParticleEmitter m_sparks{{.lifetime = 0.6F,
                                       .lifetimeVariation = 0.4F,
                                       .speed = 220.0F,
                                       .speedVariation = 0.5F,
                                       .damping = 3.0F,
                                       .startSize = 6.0F,
                                       .endSize = 1.0F,
                                       .endColor = {1.0F, 1.0F, 1.0F, 0.0F}}};
    astra2d::Renderer2D m_renderer;
    astra2d::ActionMap m_actions;
    std::mt19937 m_random;

    // Animations are data that Animators point to, so they are declared before the scene.
    const astra2d::SpriteAnimation m_idle{.frames = {0, 1, 2, 1}, .frameDuration = 0.18F};
    const astra2d::SpriteAnimation m_walk{.frames = {0, 1, 2, 1}, .frameDuration = 0.08F};
    const astra2d::SpriteAnimation m_spin{.frames = {4, 5, 6, 7}, .frameDuration = 0.1F};
    Scene m_scene;
    Entity m_player;
    astra2d::PhysicsSystem m_physics; // after the scene: it must be destroyed first
    astra2d::Connection m_onTrigger;
    astra2d::Connection m_onCollision;
    astra2d::EventDispatcher::Subscription m_onCoin;
    astra2d::EventDispatcher::Subscription m_onHit;

    State m_state = State::Title;
    int m_score = 0;
    int m_best = 0;
    int m_lives = kLives;
    float m_timeLeft = kRoundTime;
    float m_time = 0.0F;
    bool m_showPhysics = false;
    bool m_logStats = false;
    astra2d::FrameStats m_frameStats;
    astra2d::DebugOverlay m_overlay{*this};
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
