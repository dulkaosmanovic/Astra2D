#include "game.hpp"

#include "../common/example_assets.hpp"
#include "controller.hpp"
#include "level.hpp"

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/audio/audio_engine.hpp>
#include <astra2d/core/actions.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/debug/frame_stats.hpp>
#include <astra2d/debug/physics_debug_draw.hpp>
#include <astra2d/renderer/animation.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/camera_shake.hpp>
#include <astra2d/renderer/font.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/renderer/sprite.hpp>
#include <astra2d/renderer/texture.hpp>
#include <astra2d/scene/components.hpp>
#include <astra2d/scene/physics_system.hpp>
#include <astra2d/scene/scene.hpp>

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace platformer {

namespace {

using astra2d::Entity;
using astra2d::Scene;
using astra2d::SystemPhase;
using astra2d::Transform;

// ---------------------------------------------------------------------------------------------
// Tuning

constexpr std::array<std::string_view, 3> kLevelFiles = {"levels/level1.txt", "levels/level2.tmj",
                                                         "levels/level3.tmj"};
constexpr float kViewHeight = 224.0F; // world units shown vertically (14 tiles)
constexpr int kMaxHearts = 3;
constexpr float kInvulnerableTime = 1.5F; // after a hit
constexpr glm::vec2 kKnockback{150.0F, 230.0F};
constexpr float kEnemySpeed = 32.0F;
constexpr float kSquashTime = 0.6F;
constexpr float kDropTime = 0.25F; // planks ignored after dropping through one
constexpr float kFallLimit = -3.0F * kTileSize;
constexpr float kLevelClearTime = 2.5F;
constexpr float kBannerTime = 3.5F;  // level name shown at the start
constexpr float kLookAhead = 28.0F;  // the camera looks this far ahead of the player
constexpr float kCameraSpeed = 6.0F; // how quickly the camera catches up (1/s)
constexpr glm::vec2 kSpriteSize{kTileSize};

constexpr glm::vec4 kWhite{1.0F};
constexpr glm::vec4 kGold{1.0F, 0.85F, 0.3F, 1.0F};
constexpr glm::vec4 kRed{1.0F, 0.4F, 0.4F, 1.0F};
constexpr glm::vec4 kShadow{0.0F, 0.0F, 0.0F, 0.6F};

// Collision groups (Collider::category / mask): what collides with what.
constexpr std::uint16_t kWorldGroup = 1U << 0U;
constexpr std::uint16_t kPlatformGroup = 1U << 1U;
constexpr std::uint16_t kPlayerGroup = 1U << 2U;
constexpr std::uint16_t kEnemyGroup = 1U << 3U;
constexpr std::uint16_t kPickupGroup = 1U << 4U;
constexpr std::uint16_t kHazardGroup = 1U << 5U;

// Several groups as one mask. The bits are combined as unsigned: std::uint16_t operands would be
// promoted to (signed) int.
template <typename... Groups>
constexpr std::uint16_t anyOf(Groups... groups)
{
    return static_cast<std::uint16_t>((0U | ... | static_cast<unsigned>(groups)));
}

constexpr std::uint16_t without(std::uint16_t mask, std::uint16_t group)
{
    return static_cast<std::uint16_t>(static_cast<unsigned>(mask) & ~static_cast<unsigned>(group));
}

constexpr std::uint16_t kPlayerMask =
    anyOf(kWorldGroup, kPlatformGroup, kEnemyGroup, kPickupGroup, kHazardGroup);
constexpr std::uint16_t kGround = anyOf(kWorldGroup, kPlatformGroup); // what crawlers walk on

// Frames of platformer.png (see scripts/generate_platformer_assets.py).
constexpr int kCheckpointOff = 18;
constexpr int kCheckpointOn = 19;
constexpr int kHeartFull = 15;
constexpr int kHeartEmpty = 20;
constexpr int kCoinIcon = 11;

// How each level looks behind the tiles.
struct Look {
    glm::vec4 sky;
    bool hills; // parallax mountains and hills
    glm::vec4 tint;
};
constexpr std::array<Look, 3> kLooks = {{
    {.sky = {0.45F, 0.70F, 0.92F, 1.0F}, .hills = true, .tint = {1.0F, 1.0F, 1.0F, 1.0F}},
    {.sky = {0.07F, 0.06F, 0.12F, 1.0F}, .hills = false, .tint = {1.0F, 1.0F, 1.0F, 1.0F}},
    {.sky = {0.96F, 0.62F, 0.46F, 1.0F}, .hills = true, .tint = {0.85F, 0.65F, 0.80F, 1.0F}},
}};

// ---------------------------------------------------------------------------------------------
// Components of this game: plain data. The engine's Transform, SpriteRenderer, Animator,
// RigidBody, Collider and Interpolated do the rest.

struct Player {
    JumpController jump;
    bool grounded = false;
    bool facingLeft = false;
    float invulnerable = 0.0F; // seconds left
    float dropTime = 0.0F;     // seconds left of falling through planks
};

struct Enemy {
    float direction = -1.0F; // -1 left, 1 right
};

struct Squashed {
    float timeLeft = kSquashTime;
};

struct Coin {};
struct Goal {};
struct HazardTile {};
struct Effect {}; // a one-shot animation, removed when it ends

struct Checkpoint {
    bool active = false;
};

enum class State { Title, Playing, Paused, LevelClear, GameOver, Victory };

class Game : public astra2d::Application {
public:
    explicit Game(int firstLevel)
        : Application({.window = {.title = "Astra Run", .width = 1280, .height = 720}})
        , m_assets(examples::assetsDirectory())
        , m_audio({}, examples::assetsDirectory())
        , m_sheet(m_assets.texture("textures/platformer.png"),
                  {examples::kSpriteFrame, examples::kSpriteFrame},
                  {examples::kSpriteMargin, examples::kSpriteMargin},
                  {examples::kSpriteSpacing, examples::kSpriteSpacing})
        , m_tiles(m_assets.texture("textures/tiles.png"),
                  {examples::kSpriteFrame, examples::kSpriteFrame},
                  {examples::kSpriteMargin, examples::kSpriteMargin},
                  {examples::kSpriteSpacing, examples::kSpriteSpacing})
        , m_mountains(m_assets.texture("textures/mountains.png", kRepeat))
        , m_hills(m_assets.texture("textures/hills.png", kRepeat))
        , m_font(astra2d::Font::fromFile(m_assets.resolve("fonts/PressStart2P-Regular.ttf"), 8.0F))
        , m_camera(kViewHeight, framebufferSize())
        , m_shake({.maxOffset = {6.0F, 6.0F}, .maxAngle = 0.02F})
        , m_random(std::random_device{}())
        // World units are texels of the 16x16 art, so the tolerances are scaled to 16 per metre.
        , m_physics(m_scene, {.gravity = {0.0F, -1200.0F}, .lengthUnitsPerMeter = kTileSize})
    {
        // All levels are loaded up front, so a broken level file stops the game at once.
        for (const std::string_view file : kLevelFiles) {
            m_levels.push_back(loadLevel(m_assets.resolve(file)));
        }
        if (firstLevel < 0 || std::cmp_greater_equal(firstLevel, m_levels.size())) {
            throw std::out_of_range(
                std::format("There is no level {} (1 to {})", firstLevel + 1, m_levels.size()));
        }
        m_firstLevel = firstLevel;
        m_sounds = {.jump = &m_audio.sound("sounds/jump.wav"),
                    .coin = &m_audio.sound("sounds/coin.wav"),
                    .stomp = &m_audio.sound("sounds/stomp.wav"),
                    .hurt = &m_audio.sound("sounds/hurt.wav"),
                    .checkpoint = &m_audio.sound("sounds/checkpoint.wav"),
                    .win = &m_audio.sound("sounds/win.wav"),
                    .gameOver = &m_audio.sound("sounds/gameover.wav"),
                    .music = &m_audio.sound("sounds/music.wav")};
        bindControls();
        addSystems();
        connectPhysicsEvents();
        fitView(framebufferSize().x, framebufferSize().y);
        startLevel(m_firstLevel); // the title screen's backdrop
        m_state = State::Title;
        m_audio.playMusic(*m_sounds.music, 0.45F);
    }

protected:
    void onFixedUpdate(float dt) override
    {
        // The world only moves while playing; the other screens show it frozen.
        if (m_state == State::Playing) {
            m_scene.fixedUpdate(dt);
            m_levelTime += dt;
        }
    }

    void onUpdate(float dt) override
    {
        const auto& in = input();
        // A jump pressed in a frame without a fixed step is kept for the next step.
        if (m_actions.isPressed("jump", in) && m_jumpFrame != m_frame) {
            m_jumpQueued = true;
        }
        if (m_actions.isPressed("quit", in)) {
            quit();
        }
        if (m_actions.isPressed("debug", in)) {
            m_showPhysics = !m_showPhysics;
        }
        if (m_actions.isPressed("stats", in)) {
            m_logStats = !m_logStats;
            m_frameStats.restart();
            ASTRA_INFO("Frame statistics in the log: {}", m_logStats ? "on" : "off");
        }
        m_frameTime = dt;
        if (m_actions.isPressed("mute", in)) {
            m_audio.setMuted(!m_audio.muted());
        }
        switch (m_state) {
        case State::Title:
            if (m_actions.isPressed("confirm", in)) {
                startGame();
            }
            break;
        case State::Playing:
            if (m_actions.isPressed("pause", in)) {
                m_state = State::Paused;
            } else if (m_actions.isPressed("restart", in)) {
                startLevel(m_levelIndex);
            }
            break;
        case State::Paused:
            if (m_actions.isPressed("pause", in) || m_actions.isPressed("confirm", in)) {
                m_state = State::Playing;
                m_jumpQueued = false;
            }
            break;
        case State::LevelClear:
            m_stateTime -= dt;
            if (m_stateTime <= 0.0F) {
                nextLevel();
            }
            break;
        case State::GameOver:
            if (m_actions.isPressed("confirm", in)) {
                startLevel(m_levelIndex); // try the level again
            }
            break;
        case State::Victory:
            if (m_actions.isPressed("confirm", in)) {
                startGame();
            }
            break;
        }
        m_scene.update(dt); // animations and effects keep running on every screen
        m_shake.update(dt);
        m_audio.update();
        m_banner = std::max(m_banner - dt, 0.0F);
        m_time += dt;
        followPlayer(dt, false);
        ++m_frame;
    }

    void onResize(int width, int height) override { fitView(width, height); }

    void onRender() override
    {
        const Look& look = kLooks[static_cast<std::size_t>(m_levelIndex) % kLooks.size()];
        glClearColor(look.sky.r, look.sky.g, look.sky.b, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        // Whole screen pixels, so the art does not shimmer while the camera moves.
        const auto scale = static_cast<float>(m_pixelScale);
        const glm::vec2 snapped = glm::round(m_cameraPosition * scale) / scale;
        m_shake.apply(m_camera, snapped);

        if (look.hills) {
            drawBackgroundLayer(m_mountains, look.tint * glm::vec4(0.23F, 0.26F, 0.43F, 1.0F),
                                {0.15F, 0.3F}, 70.0F, look.tint);
            drawBackgroundLayer(m_hills, look.tint * glm::vec4(0.13F, 0.29F, 0.24F, 1.0F),
                                {0.4F, 0.5F}, 40.0F, look.tint);
        }
        m_renderer.begin(m_camera);
        drawTiles(m_level.decor);
        drawTiles(m_level.ground);
        m_scene.render(m_renderer, interpolationAlpha()); // every sprite, by layer
        if (m_showPhysics) {
            // Sizes in world units, where a tile is 16. Shows the merged tile boxes (inner edges
            // faint), triggers, contacts and velocities.
            astra2d::drawPhysicsDebug(m_renderer, m_physics.world(),
                                      {.lineWidth = 0.75F,
                                       .contactSize = 4.0F,
                                       .normalLength = 8.0F,
                                       .velocityScale = 0.1F});
        }
        m_renderer.end();
        drawHud();
        if (m_logStats && m_frameStats.addFrame(m_frameTime, m_renderer.stats())) {
            ASTRA_INFO("{}", m_frameStats.summary());
        }
        m_renderer.resetStats();
    }

private:
    struct Sounds {
        const astra2d::Sound* jump = nullptr;
        const astra2d::Sound* coin = nullptr;
        const astra2d::Sound* stomp = nullptr;
        const astra2d::Sound* hurt = nullptr;
        const astra2d::Sound* checkpoint = nullptr;
        const astra2d::Sound* win = nullptr;
        const astra2d::Sound* gameOver = nullptr;
        const astra2d::Sound* music = nullptr;
    };

    static constexpr astra2d::TextureSettings kRepeat{.filter = astra2d::TextureFilter::Nearest,
                                                      .wrap = astra2d::TextureWrap::Repeat};

    [[nodiscard]] glm::ivec2 framebufferSize() const
    {
        return {std::max(window().framebufferWidth(), 1),
                std::max(window().framebufferHeight(), 1)};
    }

    // Pixel art looks crisp when every texel is a whole number of screen pixels: picks the largest
    // such scale that still shows about kViewHeight units. (Not onResize() itself: the
    // constructor calls it, where a virtual call would not reach an override.)
    void fitView(int width, int height)
    {
        m_pixelScale =
            std::max(1, static_cast<int>(std::lround(static_cast<float>(height) / kViewHeight)));
        m_camera.setViewportSize(width, height);
        m_camera.setViewHeight(static_cast<float>(height) / static_cast<float>(m_pixelScale));
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
        m_actions.bind("jump", Key::Space)
            .bind("jump", Key::W)
            .bind("jump", Key::Up)
            .bind("jump", Key::Z)
            .bind("jump", GamepadButton::A);
        m_actions.bind("confirm", Key::Enter)
            .bind("confirm", Key::Space)
            .bind("confirm", GamepadButton::A)
            .bind("confirm", GamepadButton::Start);
        m_actions.bind("pause", Key::P).bind("pause", GamepadButton::Start);
        m_actions.bind("restart", Key::R).bind("restart", GamepadButton::Back);
        m_actions.bind("mute", Key::M);
        m_actions.bind("debug", Key::F3); // physics debug view
        m_actions.bind("stats", Key::F2); // FPS and render statistics in the log
        m_actions.bind("quit", Key::Escape);
    }

    // --- Systems: the gameplay, run by the Scene in this order -------------------------------

    void addSystems()
    {
        // The "physics" system was added when m_physics was made, so it runs first in every
        // fixed step and its event handlers (connectPhysicsEvents) run inside it. The systems
        // below react to where it left the bodies; the velocities they set move the bodies in the
        // next step.
        m_scene.addSystem(SystemPhase::FixedUpdate, "player control",
                          [this](Scene&, float dt) { controlPlayer(dt); });
        m_scene.addSystem(SystemPhase::FixedUpdate, "enemy patrol",
                          [this](Scene& scene, float) { patrolEnemies(scene); });
        m_scene.addSystem(SystemPhase::FixedUpdate, "dangers",
                          [this](Scene&, float dt) { checkDangers(dt); });

        m_scene.addSystem(SystemPhase::Update, "player look",
                          [this](Scene& scene, float) { animatePlayer(scene); });
        m_scene.addSystem(SystemPhase::Update, "effects", [](Scene& scene, float dt) {
            auto& registry = scene.registry();
            registry.view<Squashed>().each([&](Entity entity, Squashed& squashed) {
                squashed.timeLeft -= dt;
                if (squashed.timeLeft <= 0.0F) {
                    scene.destroyLater(entity); // safe inside a view
                }
            });
            registry.view<Effect, astra2d::Animator>().each(
                [&](Entity entity, const Effect&, const astra2d::Animator& animator) {
                if (animator.player.finished()) {
                    scene.destroyLater(entity);
                }
            });
        });
        // The built-in "animation" system (LateUpdate) then advances every Animator.
    }

    // A jump press for this fixed step: pressed in this frame (only for its first step) or
    // queued from a frame that had no step.
    bool takeJumpPress()
    {
        bool pressed = m_jumpQueued;
        m_jumpQueued = false;
        if (m_actions.isPressed("jump", input()) && m_jumpFrame != m_frame) {
            m_jumpFrame = m_frame;
            pressed = true;
        }
        return pressed;
    }

    void controlPlayer(float dt)
    {
        auto& registry = m_scene.registry();
        auto& player = registry.get<Player>(m_player);
        auto& body = registry.get<astra2d::RigidBody>(m_player);
        auto& collider = registry.get<astra2d::Collider>(m_player);
        const auto& in = input();
        const float moveX = m_actions.axis("move_x", in);
        const bool down = m_actions.axis("move_y", in) < -0.5F;
        bool jumpPressed = takeJumpPress();

        player.grounded = m_physics.isGrounded(m_player) && body.velocity.y <= 1.0F;
        if (jumpPressed && down && player.grounded && onPlank()) {
            player.dropTime = kDropTime; // drop through instead of jumping
            jumpPressed = false;
        }
        player.dropTime = std::max(player.dropTime - dt, 0.0F);
        // Planks are ignored while dropping; enemies while blinking after a hit.
        std::uint16_t mask = kPlayerMask;
        if (player.dropTime > 0.0F) {
            mask = without(mask, kPlatformGroup);
        }
        if (player.invulnerable > 0.0F) {
            mask = without(mask, kEnemyGroup);
        }
        collider.mask = mask; // applied by the physics only when it changes

        const bool jumping = player.jump.update(dt, player.grounded, jumpPressed);
        body.velocity.x = horizontalVelocity(body.velocity.x, moveX, player.grounded, m_tuning, dt);
        body.velocity.y =
            verticalVelocity(body.velocity.y, jumping, m_actions.isHeld("jump", in), m_tuning);
        body.gravityScale = body.velocity.y < 0.0F ? m_tuning.fallGravityScale : 1.0F;
        if (moveX != 0.0F) {
            player.facingLeft = moveX < 0.0F;
        }
        if (jumping) {
            playSound(*m_sounds.jump, 0.5F);
        }
    }

    // True if the player stands on a one-way plank (a short ray from the feet hits one).
    [[nodiscard]] bool onPlank() const
    {
        const glm::vec2 feet =
            m_scene.registry().get<Transform>(m_player).position - glm::vec2{0.0F, 7.5F};
        return m_physics.rayCast(feet, {0.0F, -3.0F}, kPlatformGroup).has_value();
    }

    void patrolEnemies(Scene& scene)
    {
        // Crawlers walk until a wall or the end of their floor, then turn around. Rays look ahead:
        // one forward at body height, one down just in front of the feet.
        scene.registry().view<Enemy, astra2d::RigidBody, Transform, astra2d::SpriteRenderer>().each(
            [&](Entity entity, Enemy& enemy, astra2d::RigidBody& body, const Transform& transform,
                astra2d::SpriteRenderer& sprite) {
            const glm::vec2 p = transform.position;
            const float d = enemy.direction;
            if (m_physics.isGrounded(entity)) {
                const bool wall =
                    m_physics.rayCast(p + glm::vec2{0.0F, -3.0F}, {d * 8.0F, 0.0F}, kWorldGroup)
                        .has_value();
                const bool floor =
                    m_physics.rayCast(p + glm::vec2{d * 7.0F, -4.0F}, {0.0F, -10.0F}, kGround)
                        .has_value();
                if (wall || !floor) {
                    enemy.direction = -d;
                }
            }
            body.velocity.x = enemy.direction * kEnemySpeed;
            sprite.flipX = enemy.direction < 0.0F; // the art faces right
            if (p.y < kFallLimit) {
                scene.destroyLater(entity);
            }
        });
    }

    void checkDangers(float dt)
    {
        auto& registry = m_scene.registry();
        auto& player = registry.get<Player>(m_player);
        player.invulnerable = std::max(player.invulnerable - dt, 0.0F);
        // Spikes and lava hurt as long as the player touches them (trigger enter/exit count).
        if (m_hazardContacts > 0 && player.invulnerable <= 0.0F) {
            hurt({player.facingLeft ? kKnockback.x : -kKnockback.x, kKnockback.y});
        }
        // A fall out of the level costs a heart and goes back to the last checkpoint.
        if (registry.get<Transform>(m_player).position.y < kFallLimit) {
            player.invulnerable = 0.0F; // also right after another hit
            hurt({0.0F, 0.0F});
            if (m_state == State::Playing) {
                respawn();
            }
        }
    }

    void animatePlayer(Scene& scene)
    {
        scene.registry()
            .view<Player, astra2d::RigidBody, astra2d::Animator, astra2d::SpriteRenderer>()
            .each([&](const Player& player, const astra2d::RigidBody& body,
                      astra2d::Animator& animator, astra2d::SpriteRenderer& sprite) {
            if (player.grounded) {
                animator.player.play(std::abs(body.velocity.x) > 8.0F ? m_runAnimation
                                                                      : m_idleAnimation);
            } else {
                animator.player.play(body.velocity.y > 0.0F ? m_jumpAnimation : m_fallAnimation);
            }
            sprite.flipX = player.facingLeft;
            // Blink while invulnerable.
            sprite.visible = player.invulnerable <= 0.0F || std::fmod(m_time, 0.16F) < 0.08F;
        });
    }

    // --- Physics events -----------------------------------------------------------------------

    void connectPhysicsEvents()
    {
        m_connections.push_back(m_physics.onCollisionBegin().connect(
            [this](const astra2d::CollisionEvent& event) { onCollision(event); }));
        m_connections.push_back(m_physics.onTriggerEnter().connect(
            [this](const astra2d::TriggerContact& contact) { onTriggerEnter(contact); }));
        m_connections.push_back(
            m_physics.onTriggerExit().connect([this](const astra2d::TriggerContact& contact) {
            const auto& registry = m_scene.registry();
            if (registry.valid(contact.trigger) && registry.has<HazardTile>(contact.trigger)) {
                m_hazardContacts = std::max(m_hazardContacts - 1, 0);
            }
        }));
    }

    // The player touched a crawler: from above it is stomped, otherwise the player is hurt.
    void onCollision(const astra2d::CollisionEvent& event)
    {
        auto& registry = m_scene.registry();
        if (m_state != State::Playing || (event.a != m_player && event.b != m_player)) {
            return;
        }
        const Entity other = event.other(m_player);
        if (!registry.valid(other) || !registry.has<Enemy>(other)) {
            return;
        }
        // The normal points from a to b; turned to point from the enemy to the player.
        const glm::vec2 fromEnemy = event.a == other ? event.normal : -event.normal;
        if (fromEnemy.y > 0.5F) {
            stomp(other);
        } else if (registry.get<Player>(m_player).invulnerable <= 0.0F) {
            const float side = registry.get<Transform>(m_player).position.x <
                                       registry.get<Transform>(other).position.x
                                   ? -1.0F
                                   : 1.0F;
            hurt({side * kKnockback.x, kKnockback.y});
        }
    }

    void onTriggerEnter(const astra2d::TriggerContact& contact)
    {
        auto& registry = m_scene.registry();
        const Entity thing = contact.trigger;
        if (m_state != State::Playing || contact.other != m_player || !registry.valid(thing)) {
            return;
        }
        const glm::vec2 position = registry.get<Transform>(thing).position;
        if (registry.has<Coin>(thing)) {
            registry.remove<Coin>(thing); // counted once, even if the trigger fires again
            m_scene.destroyLater(thing);
            ++m_coins;
            playSound(*m_sounds.coin, 0.45F);
            spawnSparkle(position);
        } else if (registry.has<HazardTile>(thing)) {
            ++m_hazardContacts;
        } else if (auto* checkpoint = registry.tryGet<Checkpoint>(thing);
                   checkpoint != nullptr && !checkpoint->active) {
            checkpoint->active = true;
            registry.get<astra2d::SpriteRenderer>(thing).sprite = m_sheet.frame(kCheckpointOn);
            m_respawn = position;
            playSound(*m_sounds.checkpoint, 0.6F);
            spawnSparkle(position + glm::vec2{0.0F, 6.0F});
        } else if (registry.has<Goal>(thing)) {
            m_state = State::LevelClear;
            m_stateTime = kLevelClearTime;
            m_totalCoins += m_coins;
            m_totalTime += m_levelTime;
            m_audio.play(*m_sounds.win, {.volume = 0.6F});
            spawnSparkle(position + glm::vec2{0.0F, 8.0F});
        }
    }

    void stomp(Entity enemy)
    {
        auto& registry = m_scene.registry();
        // Without a body and collider it is no longer simulated; it stays squashed for a moment.
        registry.remove<Enemy>(enemy);
        registry.remove<astra2d::RigidBody>(enemy);
        registry.remove<astra2d::Collider>(enemy);
        registry.emplace<Squashed>(enemy);
        registry.get<astra2d::Animator>(enemy).player.play(m_squashAnimation);

        auto& player = registry.get<Player>(m_player);
        auto& body = registry.get<astra2d::RigidBody>(m_player);
        // Holding jump bounces as high as a jump.
        body.velocity.y =
            m_actions.isHeld("jump", input()) ? m_tuning.jumpSpeed : m_tuning.stompBounce;
        player.jump.reset();
        playSound(*m_sounds.stomp, 0.7F);
        m_shake.addTrauma(0.25F);
        spawnSparkle(registry.get<Transform>(enemy).position);
    }

    void hurt(glm::vec2 knockback)
    {
        auto& registry = m_scene.registry();
        auto& player = registry.get<Player>(m_player);
        if (player.invulnerable > 0.0F) {
            return;
        }
        --m_hearts;
        player.invulnerable = kInvulnerableTime;
        registry.get<astra2d::RigidBody>(m_player).velocity = knockback;
        m_shake.addTrauma(0.6F);
        if (m_hearts <= 0) {
            m_state = State::GameOver;
            m_audio.play(*m_sounds.gameOver, {.volume = 0.7F});
        } else {
            playSound(*m_sounds.hurt, 0.6F);
        }
    }

    void respawn()
    {
        auto& registry = m_scene.registry();
        registry.get<Transform>(m_player).position = m_respawn;
        registry.get<astra2d::RigidBody>(m_player).velocity = glm::vec2{0.0F};
        registry.get<Player>(m_player).jump.reset();
        m_scene.resetInterpolation(m_player); // a teleport, not a slide
        followPlayer(0.0F, true);
    }

    // --- Levels -------------------------------------------------------------------------------

    void startGame()
    {
        m_totalCoins = 0;
        m_totalTime = 0.0F;
        startLevel(m_firstLevel);
    }

    void nextLevel()
    {
        if (m_levelIndex + 1 < static_cast<int>(m_levels.size())) {
            startLevel(m_levelIndex + 1);
        } else {
            m_state = State::Victory;
        }
    }

    // Replaces the scene with level @p index and starts playing it.
    void startLevel(int index)
    {
        m_scene.clear(); // the physics system drops the bodies with their components
        m_levelIndex = index;
        m_level = m_levels[static_cast<std::size_t>(index)];
        m_state = State::Playing;
        m_hearts = kMaxHearts;
        m_coins = 0;
        m_levelTime = 0.0F;
        m_hazardContacts = 0;
        m_banner = kBannerTime;
        m_jumpQueued = false; // e.g. the Space that confirmed a menu
        m_shake.reset();
        auto& registry = m_scene.registry();

        // The tile map as two static colliders: solid tiles, and one-way planks in their own
        // group so the player can drop through them.
        const Entity solid = m_scene.createEntity("solid");
        registry.emplace<astra2d::TilemapCollider>(
            solid, astra2d::TilemapCollider{.grid = m_level.solid, .category = kWorldGroup});
        const Entity planks = m_scene.createEntity("planks");
        registry.emplace<astra2d::TilemapCollider>(
            planks,
            astra2d::TilemapCollider{.grid = m_level.platforms, .category = kPlatformGroup});
        for (const Hazard& hazard : m_level.hazards) {
            spawnHazard(hazard);
        }
        for (const Thing& thing : m_level.things) {
            switch (thing.kind) {
            case ThingKind::Player:
                spawnPlayer(thing.position);
                break;
            case ThingKind::Coin:
                spawnCoin(thing.position);
                break;
            case ThingKind::Enemy:
                spawnEnemy(thing.position);
                break;
            case ThingKind::Checkpoint:
                spawnCheckpoint(thing.position);
                break;
            case ThingKind::Goal:
                spawnGoal(thing.position);
                break;
            }
        }
        followPlayer(0.0F, true);
    }

    void spawnPlayer(glm::vec2 position)
    {
        m_player = m_scene.createEntity("player", position);
        m_respawn = position;
        auto& registry = m_scene.registry();
        registry.emplace<Player>(m_player);
        registry.emplace<astra2d::SpriteRenderer>(
            m_player, astra2d::SpriteRenderer{.size = kSpriteSize, .layer = 3});
        registry.emplace<astra2d::Animator>(m_player, astra2d::Animator{.sheet = &m_sheet})
            .player.play(m_idleAnimation);
        // A box a little narrower than the sprite, without friction so it slides along walls;
        // the controller sets its velocity directly.
        registry.emplace<astra2d::RigidBody>(
            m_player, astra2d::RigidBody{.fixedRotation = true, .allowSleep = false});
        registry.emplace<astra2d::Collider>(m_player, astra2d::Collider{.halfExtents = {4.5F, 7.0F},
                                                                        .offset = {0.0F, -1.0F},
                                                                        .friction = 0.0F,
                                                                        .category = kPlayerGroup,
                                                                        .mask = kPlayerMask});
        registry.emplace<astra2d::Interpolated>(m_player);
    }

    void spawnCoin(glm::vec2 position)
    {
        const Entity coin = m_scene.createEntity("coin", position);
        auto& registry = m_scene.registry();
        registry.emplace<Coin>(coin);
        registry.emplace<astra2d::SpriteRenderer>(
            coin, astra2d::SpriteRenderer{.size = kSpriteSize, .layer = 1});
        registry.emplace<astra2d::Animator>(coin, astra2d::Animator{.sheet = &m_sheet})
            .player.play(m_spinAnimation);
        registry.emplace<astra2d::Collider>(coin, trigger({5.0F, 6.0F}, {}, kPickupGroup));
    }

    void spawnEnemy(glm::vec2 position)
    {
        const Entity enemy = m_scene.createEntity("crawler", position);
        auto& registry = m_scene.registry();
        registry.emplace<Enemy>(enemy);
        registry.emplace<astra2d::SpriteRenderer>(
            enemy, astra2d::SpriteRenderer{.size = kSpriteSize, .layer = 2});
        registry.emplace<astra2d::Animator>(enemy, astra2d::Animator{.sheet = &m_sheet})
            .player.play(m_crawlAnimation);
        registry.emplace<astra2d::RigidBody>(
            enemy, astra2d::RigidBody{.fixedRotation = true, .allowSleep = false});
        // Enemies pass through each other, so two never get stuck turning back and forth.
        registry.emplace<astra2d::Collider>(
            enemy, astra2d::Collider{.halfExtents = {6.0F, 4.5F},
                                     .offset = {0.0F, -3.5F},
                                     .friction = 0.0F,
                                     .category = kEnemyGroup,
                                     .mask = anyOf(kGround, kPlayerGroup)});
        registry.emplace<astra2d::Interpolated>(enemy);
    }

    void spawnCheckpoint(glm::vec2 position)
    {
        const Entity checkpoint = m_scene.createEntity("checkpoint", position);
        auto& registry = m_scene.registry();
        registry.emplace<Checkpoint>(checkpoint);
        registry.emplace<astra2d::SpriteRenderer>(
            checkpoint, astra2d::SpriteRenderer{.sprite = m_sheet.frame(kCheckpointOff),
                                                .size = kSpriteSize,
                                                .layer = 1});
        registry.emplace<astra2d::Collider>(checkpoint, trigger({5.0F, 8.0F}, {}, kPickupGroup));
    }

    void spawnGoal(glm::vec2 position)
    {
        const Entity goal = m_scene.createEntity("goal", position);
        auto& registry = m_scene.registry();
        registry.emplace<Goal>(goal);
        registry.emplace<astra2d::SpriteRenderer>(
            goal, astra2d::SpriteRenderer{.size = kSpriteSize, .layer = 1});
        registry.emplace<astra2d::Animator>(goal, astra2d::Animator{.sheet = &m_sheet})
            .player.play(m_waveAnimation);
        registry.emplace<astra2d::Collider>(goal, trigger({5.0F, 8.0F}, {}, kPickupGroup));
    }

    void spawnHazard(const Hazard& hazard)
    {
        // Spikes hurt in their lower half, lava below its surface. Drawn as tiles, so only the
        // trigger is an entity.
        const Entity entity =
            m_scene.createEntity("hazard", m_level.cellCenter(hazard.x, hazard.y));
        m_scene.registry().emplace<HazardTile>(entity);
        m_scene.registry().emplace<astra2d::Collider>(
            entity, hazard.frame == tile::kLava
                        ? trigger({8.0F, 5.0F}, {0.0F, -3.0F}, kHazardGroup)
                        : trigger({6.5F, 3.0F}, {0.0F, -5.0F}, kHazardGroup));
    }

    void spawnSparkle(glm::vec2 position)
    {
        const Entity sparkle = m_scene.createEntity({}, position);
        auto& registry = m_scene.registry();
        registry.emplace<Effect>(sparkle);
        registry.emplace<astra2d::SpriteRenderer>(
            sparkle, astra2d::SpriteRenderer{.size = kSpriteSize, .layer = 4});
        registry.emplace<astra2d::Animator>(sparkle, astra2d::Animator{.sheet = &m_sheet})
            .player.play(m_sparkleAnimation);
    }

    // A static trigger box that only the player sets off.
    static astra2d::Collider trigger(glm::vec2 halfExtents, glm::vec2 offset,
                                     std::uint16_t category)
    {
        return {.halfExtents = halfExtents,
                .offset = offset,
                .isTrigger = true,
                .category = category,
                .mask = kPlayerGroup};
    }

    void playSound(const astra2d::Sound& sound, float volume)
    {
        // A slightly different pitch every time keeps repeated sounds from getting tiring.
        std::uniform_real_distribution<float> pitch(0.94F, 1.06F);
        m_audio.play(sound, {.volume = volume, .pitch = pitch(m_random)});
    }

    // --- Camera and drawing -------------------------------------------------------------------

    // Moves the camera towards the player (at once if @p snap), a little ahead in the direction
    // they face, and keeps it inside the level.
    void followPlayer(float dt, bool snap)
    {
        const auto& registry = m_scene.registry();
        if (!registry.valid(m_player)) {
            return;
        }
        const glm::vec2 player = m_scene.worldTransform(m_player, interpolationAlpha()).position;
        const bool left = registry.get<Player>(m_player).facingLeft;
        const glm::vec2 target = player + glm::vec2{left ? -kLookAhead : kLookAhead, 16.0F};
        m_cameraPosition = snap ? target
                                : m_cameraPosition + ((target - m_cameraPosition) *
                                                      (1.0F - std::exp(-kCameraSpeed * dt)));
        const glm::vec2 half = m_camera.visibleSize() * 0.5F;
        const glm::vec2 size = m_level.size();
        // A level narrower or lower than the view is shown from its left / bottom edge.
        m_cameraPosition.x =
            std::clamp(m_cameraPosition.x, half.x, std::max(half.x, size.x - half.x));
        m_cameraPosition.y =
            std::clamp(m_cameraPosition.y, half.y, std::max(half.y, size.y - half.y));
    }

    // An endless strip of a tiling texture that scrolls slower than the world (parallax), filled
    // below with @p fill.
    void drawBackgroundLayer(const astra2d::Texture2D& texture, glm::vec4 fill, glm::vec2 parallax,
                             float y, glm::vec4 tint)
    {
        const astra2d::OrthographicCamera layer = m_camera.withParallax(parallax);
        const glm::vec2 min = layer.visibleMin();
        const glm::vec2 max = layer.visibleMax();
        const glm::vec2 tile(texture.size());
        const float width = max.x - min.x;
        const float centerX = (min.x + max.x) * 0.5F;
        const float bottom = y - (tile.y * 0.5F);
        m_renderer.begin(layer);
        m_renderer.drawTiled({centerX, y}, {width, tile.y}, texture, tile, tint, {0.0F, bottom});
        if (bottom > min.y) {
            m_renderer.drawQuad({centerX, (bottom + min.y) * 0.5F}, {width, bottom - min.y}, fill);
        }
        m_renderer.end();
    }

    // The tiles of @p layer that the camera sees.
    void drawTiles(const std::vector<int>& layer)
    {
        const glm::vec2 min = m_camera.visibleMin() / kTileSize;
        const glm::vec2 max = m_camera.visibleMax() / kTileSize;
        const int x0 = std::max(static_cast<int>(std::floor(min.x)) - 1, 0);
        const int y0 = std::max(static_cast<int>(std::floor(min.y)) - 1, 0);
        const int x1 = std::min(static_cast<int>(std::ceil(max.x)) + 1, m_level.width - 1);
        const int y1 = std::min(static_cast<int>(std::ceil(max.y)) + 1, m_level.height - 1);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                if (const int frame = m_level.at(layer, x, y); frame != tile::kNone) {
                    m_renderer.drawSprite(m_level.cellCenter(x, y), kSpriteSize,
                                          m_tiles.frame(frame));
                }
            }
        }
    }

    // --- HUD, in screen pixels ----------------------------------------------------------------

    void drawText(std::string_view text, glm::vec2 topLeft, float size, glm::vec4 color)
    {
        const float shadow = std::max(1.0F, size / 8.0F);
        m_renderer.drawText(m_font, text, topLeft + glm::vec2{shadow, -shadow}, size, kShadow);
        m_renderer.drawText(m_font, text, topLeft, size, color);
    }

    // Centred text, made smaller (in whole steps of the 8 pixel font, so it stays crisp) if it
    // does not fit the window's width.
    void drawCentered(std::string_view text, float y, float size, glm::vec4 color)
    {
        const float width = static_cast<float>(framebufferSize().x);
        const float fits = 0.94F * width / m_font.atlas().measure(text, 1.0F).x;
        size = std::max(8.0F, std::min(size, std::floor(fits / 8.0F) * 8.0F));
        const glm::vec2 extent = m_font.atlas().measure(text, size);
        drawText(text, {std::round((width - extent.x) * 0.5F), y}, size, color);
    }

    void drawHud()
    {
        const glm::vec2 screen(framebufferSize());
        // The world's pixel scale, but small enough that the HUD fits a narrow window.
        const float s = std::max(
            1.0F, std::min(static_cast<float>(m_pixelScale), std::floor(screen.x / 320.0F)));
        const float text = 8.0F * s;
        const float top = screen.y - (8.0F * s);
        m_renderer.begin(
            astra2d::OrthographicCamera::screenSpace(framebufferSize().x, framebufferSize().y));
        if (m_state != State::Title) {
            for (int i = 0; i < kMaxHearts; ++i) {
                const glm::vec2 at{(16.0F + (18.0F * static_cast<float>(i))) * s, top - (8.0F * s)};
                m_renderer.drawSprite(at, glm::vec2{16.0F * s},
                                      m_sheet.frame(i < m_hearts ? kHeartFull : kHeartEmpty));
            }
            m_renderer.drawSprite({16.0F * s, top - (28.0F * s)}, glm::vec2{16.0F * s},
                                  m_sheet.frame(kCoinIcon));
            drawText(std::format("{}/{}", m_coins, m_level.count(ThingKind::Coin)),
                     {28.0F * s, top - (24.0F * s)}, text, kGold);
            const std::string time =
                std::format("TIME {}:{:02}", static_cast<int>(m_levelTime) / 60,
                            static_cast<int>(m_levelTime) % 60);
            const float width = m_font.atlas().measure(time, text).x;
            drawText(time, {screen.x - width - (12.0F * s), top - (4.0F * s)}, text, kWhite);
            drawText(std::format("LEVEL {}", m_levelIndex + 1),
                     {screen.x - m_font.atlas().measure("LEVEL 1", text).x - (12.0F * s),
                      top - (18.0F * s)},
                     text, kWhite);
        }
        if (m_audio.muted()) {
            drawText("MUTED", {12.0F * s, 16.0F * s}, text, kWhite);
        }
        const float middle = screen.y * 0.5F;
        switch (m_state) {
        case State::Title:
            dim(screen, 0.35F);
            drawCentered("ASTRA RUN", middle + (48.0F * s), 32.0F * s, kGold);
            drawCentered("a tiny astronaut, three worlds, one flag each", middle, text, kWhite);
            drawCentered("RUN  A/D   JUMP  SPACE   DROP  S+SPACE", middle - (22.0F * s), text,
                         kWhite);
            drawCentered("PAUSE  P   RESTART  R   MUTE  M", middle - (36.0F * s), text, kWhite);
            if (std::fmod(m_time, 1.0F) < 0.7F) {
                drawCentered("press ENTER or A to start", middle - (64.0F * s), text, kGold);
            }
            break;
        case State::Playing:
            if (m_banner > 0.0F) {
                const float alpha = std::min(m_banner, 1.0F);
                drawCentered(m_level.name, top - (40.0F * s), 16.0F * s,
                             {kGold.r, kGold.g, kGold.b, alpha});
                drawCentered(m_level.hint, top - (64.0F * s), text, {1.0F, 1.0F, 1.0F, alpha});
            }
            break;
        case State::Paused:
            dim(screen, 0.5F);
            drawCentered("PAUSED", middle + (8.0F * s), 16.0F * s, kWhite);
            drawCentered("P or ENTER to continue", middle - (16.0F * s), text, kWhite);
            break;
        case State::LevelClear:
            drawCentered("LEVEL CLEAR!", middle + (24.0F * s), 16.0F * s, kGold);
            drawCentered(std::format("coins {}/{}", m_coins, m_level.count(ThingKind::Coin)),
                         middle - (4.0F * s), text, kWhite);
            break;
        case State::GameOver:
            dim(screen, 0.55F);
            drawCentered("GAME OVER", middle + (24.0F * s), 24.0F * s, kRed);
            drawCentered("ENTER to try again   ESC to quit", middle - (16.0F * s), text, kWhite);
            break;
        case State::Victory: {
            dim(screen, 0.55F);
            int allCoins = 0;
            for (const Level& level : m_levels) {
                allCoins += level.count(ThingKind::Coin);
            }
            drawCentered("YOU MADE IT HOME!", middle + (40.0F * s), 16.0F * s, kGold);
            drawCentered(std::format("coins {}/{}", m_totalCoins, allCoins), middle + (8.0F * s),
                         text, kWhite);
            drawCentered(std::format("time {}:{:02}", static_cast<int>(m_totalTime) / 60,
                                     static_cast<int>(m_totalTime) % 60),
                         middle - (8.0F * s), text, kWhite);
            drawCentered("ENTER to play again", middle - (40.0F * s), text, kGold);
            break;
        }
        }
        m_renderer.end();
    }

    void dim(glm::vec2 screen, float alpha)
    {
        m_renderer.drawQuad(screen * 0.5F, screen, {0.0F, 0.0F, 0.0F, alpha});
    }

    // --- State --------------------------------------------------------------------------------

    astra2d::AssetCache m_assets;
    astra2d::AudioEngine m_audio;
    Sounds m_sounds;
    astra2d::SpriteSheet m_sheet;
    astra2d::SpriteSheet m_tiles;
    const astra2d::Texture2D& m_mountains;
    const astra2d::Texture2D& m_hills;
    astra2d::Font m_font;
    astra2d::OrthographicCamera m_camera;
    astra2d::CameraShake m_shake;
    astra2d::Renderer2D m_renderer;
    astra2d::ActionMap m_actions;
    std::mt19937 m_random;
    MoveTuning m_tuning;

    // Animations are data that Animators point to, so they are declared before the scene.
    const astra2d::SpriteAnimation m_idleAnimation{.frames = {0, 1}, .frameDuration = 0.5F};
    const astra2d::SpriteAnimation m_runAnimation{.frames = {2, 3, 4, 5}, .frameDuration = 0.09F};
    const astra2d::SpriteAnimation m_jumpAnimation{.frames = {6}};
    const astra2d::SpriteAnimation m_fallAnimation{.frames = {7}};
    const astra2d::SpriteAnimation m_crawlAnimation{.frames = {8, 9}, .frameDuration = 0.2F};
    const astra2d::SpriteAnimation m_squashAnimation{.frames = {10}};
    const astra2d::SpriteAnimation m_spinAnimation{.frames = {11, 12, 13, 14},
                                                   .frameDuration = 0.12F};
    const astra2d::SpriteAnimation m_waveAnimation{.frames = {16, 17}, .frameDuration = 0.3F};
    const astra2d::SpriteAnimation m_sparkleAnimation{
        .frames = {21, 22, 23}, .frameDuration = 0.07F, .loop = false};
    Scene m_scene;
    astra2d::PhysicsSystem m_physics; // after the scene: it must be destroyed first
    std::vector<astra2d::Connection> m_connections;

    std::vector<Level> m_levels;
    int m_firstLevel = 0;
    Level m_level; // the one being played
    int m_levelIndex = 0;
    Entity m_player;
    glm::vec2 m_respawn{0.0F};
    glm::vec2 m_cameraPosition{0.0F};
    int m_pixelScale = 1;
    bool m_showPhysics = false;
    bool m_logStats = false;
    astra2d::FrameStats m_frameStats;
    float m_frameTime = 0.0F;

    State m_state = State::Title;
    float m_stateTime = 0.0F;
    int m_hearts = kMaxHearts;
    int m_coins = 0;
    int m_totalCoins = 0;
    int m_hazardContacts = 0;
    float m_levelTime = 0.0F;
    float m_totalTime = 0.0F;
    float m_banner = 0.0F;
    float m_time = 0.0F;
    std::uint64_t m_frame = 0;
    std::uint64_t m_jumpFrame = ~std::uint64_t{0};
    bool m_jumpQueued = false;
};

} // namespace

void runGame(int firstLevel)
{
    Game game(firstLevel);
    game.run();
}

} // namespace platformer
