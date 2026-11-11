#include "game.hpp"

#include "../common/example_assets.hpp"
#include "../common/hud.hpp"
#include "controller.hpp"
#include "level.hpp"
#include "looks.hpp"

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/audio/audio_engine.hpp>
#include <astra2d/core/actions.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/debug/debug_overlay.hpp>
#include <astra2d/debug/frame_stats.hpp>
#include <astra2d/debug/physics_debug_draw.hpp>
#include <astra2d/renderer/animation.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/camera_follow.hpp>
#include <astra2d/renderer/camera_shake.hpp>
#include <astra2d/renderer/font.hpp>
#include <astra2d/renderer/particles.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/renderer/sprite.hpp>
#include <astra2d/renderer/texture.hpp>
#include <astra2d/scene/components.hpp>
#include <astra2d/scene/physics_system.hpp>
#include <astra2d/scene/scene.hpp>

#include <glad/gl.h>
#include <imgui.h>
#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
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

constexpr std::array<std::string_view, 4> kLevelFiles = {"levels/level1.txt", "levels/level2.tmj",
                                                         "levels/level3.tmj", "levels/level4.tmj"};
constexpr float kViewHeight = 224.0F; // world units shown vertically (14 tiles)
constexpr float kGravity = 1200.0F;   // world units/s²
constexpr int kMaxHearts = 3;
constexpr float kInvulnerableTime = 1.5F; // after a hit
constexpr float kHurtPoseTime = 0.35F;    // the hurt frame is shown this long after a hit
constexpr glm::vec2 kKnockback{150.0F, 230.0F};
constexpr float kCrawlerSpeed = 32.0F;
constexpr float kSpikerSpeed = 22.0F;
constexpr float kFlyerSpeed = 30.0F;
constexpr float kFlyerRange = 3.0F * kTileSize; // flies this far to each side of its start
constexpr float kFlyerBob = 6.0F;               // and this far up and down
constexpr float kSpringSpeed = 600.0F;          // about 9 tiles high
constexpr glm::vec2 kPlatformTravel{4.0F * kTileSize, 0.0F};
constexpr glm::vec2 kLiftTravel{0.0F, 4.0F * kTileSize};
constexpr float kMoverPeriod = 4.0F;     // seconds there and back
constexpr float kLandingSpeed = -220.0F; // landing faster than this raises dust
constexpr float kSquashTime = 0.6F;
constexpr float kDropTime = 0.25F; // planks ignored after dropping through one
constexpr float kFallLimit = -3.0F * kTileSize;
constexpr float kLevelClearTime = 4.0F;
constexpr float kLiftOffPanelTime = 1.8F; // the panel waits until the rocket has flown out of view
constexpr float kLiftOffDelay = 0.5F;     // the rocket waits this long after the player boards
constexpr float kLiftOffAccel = 260.0F;   // then speeds up like this
constexpr float kBannerTime = 3.5F;       // level name shown at the start
constexpr float kLookAhead = 28.0F;       // the camera looks this far ahead of the player
constexpr float kCameraSpeed = 6.0F;      // how quickly the camera catches up (1/s)
constexpr glm::vec2 kSpriteSize{kTileSize};
constexpr int kBigFrame = 24; // texels of a frame of astronaut.png, which stands on row 19
constexpr glm::vec2 kBigSize{static_cast<float>(kBigFrame)};

constexpr glm::vec4 kWhite{1.0F};
constexpr glm::vec4 kGold{1.0F, 0.85F, 0.3F, 1.0F};
constexpr glm::vec4 kRed{1.0F, 0.4F, 0.4F, 1.0F};
constexpr glm::vec4 kShadow{0.0F, 0.0F, 0.0F, 0.6F};
// Decoration is darker than the ground and everything the player meets, so it reads as background.
constexpr glm::vec4 kDecorTint{0.75F, 0.75F, 0.85F, 1.0F};

// Collision groups (Collider::category / mask): what collides with what.
constexpr std::uint16_t kWorldGroup = 1U << 0U;
constexpr std::uint16_t kPlatformGroup = 1U << 1U;
constexpr std::uint16_t kPlayerGroup = 1U << 2U;
constexpr std::uint16_t kEnemyGroup = 1U << 3U;
constexpr std::uint16_t kPickupGroup = 1U << 4U;
constexpr std::uint16_t kHazardGroup = 1U << 5U;

constexpr std::uint16_t kPlayerMask =
    astra2d::collisionGroups(kWorldGroup, kPlatformGroup, kEnemyGroup, kPickupGroup, kHazardGroup);
constexpr std::uint16_t kGround =
    astra2d::collisionGroups(kWorldGroup, kPlatformGroup); // what crawlers walk on

// Frames of platformer.png (see scripts/generate_platformer_assets.py).
constexpr int kCheckpointOff = 18;
constexpr int kCheckpointOn = 19;
constexpr int kHeartFull = 15;
constexpr int kHeartEmpty = 20;
constexpr int kCoinIcon = 11;
constexpr int kSparkle = 21; // and 22, 23
// Frames of astronaut.png: the rocket's upper half is drawn 20 texels above its lower half (the
// goal), the exhaust 18 below.
constexpr int kAstronautIdle = 0;
constexpr int kAstronautWave = 14; // and 15
constexpr int kFlag = 16;
constexpr int kRocketBottom = 9;
constexpr int kRocketTop = 11;
constexpr float kRocketTopOffset = 20.0F;
constexpr float kExhaustOffset = 18.0F;
constexpr float kPlanetSize = 32.0F; // texels of a frame of planets.png
constexpr int kSpringFrame = 30;
constexpr int kPlatformLeft = 35; // then the middle and the right end

// ---------------------------------------------------------------------------------------------
// Components of this game: plain data. The engine's Transform, SpriteRenderer, Animator,
// RigidBody, Collider and Interpolated do the rest.

struct Player {
    JumpController jump;
    bool grounded = false;
    bool wasGrounded = true; // in the step before, to notice landings
    bool facingLeft = false;
    bool launched = false;     // thrown up by a spring: releasing jump does not cut it short
    float invulnerable = 0.0F; // seconds left
    float dropTime = 0.0F;     // seconds left of falling through planks
    float carryX = 0.0F;       // horizontal speed of the moving platform stood on
    float lastVelocityY = 0.0F;
};

// Hurts the player on touch; from above it is stomped, unless it is spiky.
struct Enemy {
    bool stompable = true;
};

// Walks along its floor and turns at walls and ledges (crawlers, spikers).
struct Walker {
    float speed = kCrawlerSpeed;
    float direction = -1.0F; // -1 left, 1 right
};

// Flies back and forth around where it started, bobbing up and down (bats).
struct Flyer {
    glm::vec2 home{0.0F};
    float direction = -1.0F;
    float time = 0.0F;
};

// A kinematic platform that goes from origin to origin + travel and back.
struct Mover {
    glm::vec2 origin{0.0F};
    glm::vec2 travel{0.0F};
    float time = 0.0F;
};

struct Coin {};
struct HeartPickup {};
struct Spring {};
// The parked rocket: its lower half is the goal's trigger, the nose and exhaust are separate
// sprites that move with it when it lifts off.
struct Goal {
    glm::vec2 base{0.0F};
    Entity top;
    Entity exhaust;
};
struct HazardTile {};

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
        , m_big(m_assets.texture("textures/astronaut.png"), {kBigFrame, kBigFrame},
                {examples::kSpriteMargin, examples::kSpriteMargin},
                {examples::kSpriteSpacing, examples::kSpriteSpacing})
        , m_planets(m_assets.texture("textures/planets.png"), {kPlanetSize, kPlanetSize},
                    {examples::kSpriteMargin, examples::kSpriteMargin},
                    {examples::kSpriteSpacing, examples::kSpriteSpacing})
        , m_font(astra2d::Font::fromFile(m_assets.resolve("fonts/PressStart2P-Regular.ttf"), 8.0F))
        , m_camera(kViewHeight, framebufferSize())
        , m_shake({.maxOffset = {6.0F, 6.0F}, .maxAngle = 0.02F})
        , m_random(std::random_device{}())
        // World units are texels of the 16x16 art, so the tolerances are scaled to 16 per metre.
        , m_physics(m_scene, {.gravity = {0.0F, -kGravity}, .lengthUnitsPerMeter = kTileSize})
    {
        // All levels and backgrounds are loaded up front, so a broken file stops the game at once.
        for (const std::string_view file : kLevelFiles) {
            m_levels.push_back(loadLevel(m_assets.resolve(file)));
        }
        (void)m_assets.texture(kNebula.texture, kRepeat);
        (void)m_assets.texture(kStars.texture, kRepeat);
        (void)m_assets.texture("textures/rings.png");
        (void)m_assets.texture("textures/earth.png");
        for (const Look& look : kLooks) {
            for (const Layer& layer : look.layers) {
                if (!layer.texture.empty()) {
                    (void)m_assets.texture(layer.texture, kRepeat);
                }
            }
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
        setUpOverlay();
        // Whole screen pixels per texel, about kViewHeight texels high, so the art stays crisp.
        m_camera.setPixelPerfect(kViewHeight);
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
                window().input().clearFixedStepPresses(); // the key that resumed is no jump
            }
            break;
        case State::LevelClear:
            m_stateTime -= dt;
            liftOff(kLevelClearTime - m_stateTime - kLiftOffDelay, dt);
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
        m_smoke.update(dt);
        m_audio.update();
        m_banner = std::max(m_banner - dt, 0.0F);
        driftWeather(dt);
        m_time += dt;
        followPlayer(dt, false);
    }

    void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }

    void onRender() override
    {
        // After the last planet the game ends on Earth instead of in the level.
        const Look& look = m_state == State::Victory ? kEndingLook : currentLook();
        glClearColor(look.skyBottom.r, look.skyBottom.g, look.skyBottom.b, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        if (m_state == State::Victory) {
            drawEnding();
        } else {
            drawWorld(look);
        }
        drawHud();
        m_overlay.addFrame(m_renderer.stats());
        if (m_logStats && m_frameStats.addFrame(frameTime(), m_renderer.stats())) {
            ASTRA_INFO("{}", m_frameStats.summary());
        }
        m_renderer.resetStats();
        m_overlay.draw(); // last, on top of the HUD
    }

private:
    // The level: its sky and background layers, tiles, sprites and weather, and the physics debug
    // view if it is on.
    void drawWorld(const Look& look)
    {
        m_shake.apply(m_camera, m_follow.position());

        drawSky(look);
        for (const Layer& layer : look.layers) {
            if (!layer.texture.empty()) {
                drawBackgroundLayer(layer);
            }
        }
        m_renderer.begin(m_camera);
        m_renderer.drawTileLayer(m_level.decor, m_tiles, kDecorTint);
        m_renderer.drawTileLayer(m_level.ground, m_tiles);
        m_smoke.draw(m_renderer); // behind the rocket
        drawMovers();
        m_scene.render(m_renderer, interpolationAlpha()); // every sprite, by layer
        drawWeather(look.weather);
        if (m_showPhysics) {
            astra2d::drawPhysicsDebug(m_renderer, m_physics.world(), m_physicsDebug);
        }
        m_overlay.drawSelection(m_renderer, 1.0F); // the entity picked in the F1 overlay
        m_renderer.end();
    }

    // The ending: Earth's horizon across the bottom of the screen and the astronaut waving on top
    // of it, between their flag and the landed rocket, under a twinkling sky. In screen space: a
    // texel of earth.png is a HUD pixel, the sprites are twice as big.
    void drawEnding()
    {
        drawSky(kEndingLook);
        const glm::vec2 screen(framebufferSize());
        const float s = hudScale(screen);
        const astra2d::Texture2D& earth = m_assets.texture("textures/earth.png");
        const float radius = 120.0F * s; // of the planet in earth.png
        const glm::vec2 center{std::round(screen.x * 0.5F), (70.0F * s) - radius};
        const float k = 2.0F * s;
        m_renderer.begin(
            astra2d::OrthographicCamera::screenSpace(framebufferSize().x, framebufferSize().y));
        m_renderer.drawQuad(center, glm::vec2(earth.size()) * s, earth);
        // A 24x24 frame's art stands on row 19, 8 texels below its centre. Stood on the planet
        // @p angle radians from its top, leaning with the ground; @p lift raises it (texels).
        const auto standOn = [&](float angle, int frame, float lift) {
            const glm::vec2 up{std::sin(angle), std::cos(angle)};
            m_renderer.drawSprite(center + (up * (radius - s + ((8.0F + lift) * k))), kBigSize * k,
                                  m_big.frame(frame), kWhite, -angle);
        };
        standOn(-0.4F, kFlag, 0.0F);
        standOn(0.5F, kRocketBottom, 0.0F);
        standOn(0.5F, kRocketTop, kRocketTopOffset);
        standOn(0.0F, kAstronautWave + (static_cast<int>(m_time * 3.0F) % 2), 0.0F);
        constexpr std::array<glm::vec2, 7> kSparkles = {{{0.12F, 0.30F},
                                                         {0.85F, 0.36F},
                                                         {0.30F, 0.46F},
                                                         {0.70F, 0.52F},
                                                         {0.06F, 0.58F},
                                                         {0.93F, 0.62F},
                                                         {0.50F, 0.40F}}};
        for (std::size_t i = 0; i < kSparkles.size(); ++i) { // twinkling, each in its own time
            const int step = static_cast<int>((m_time * 5.0F) + (static_cast<float>(i) * 1.7F)) % 6;
            if (step < 3) {
                m_renderer.drawSprite(glm::round(kSparkles[i] * screen), glm::vec2{16.0F * s},
                                      m_sheet.frame(kSparkle + step));
            }
        }
        m_renderer.end();
    }

    // The HUD's pixel scale: the world's, but small enough that the HUD fits a narrow window.
    [[nodiscard]] float hudScale(glm::vec2 screen) const
    {
        return std::max(1.0F, std::min(m_camera.pixelsPerUnit(), std::floor(screen.x / 320.0F)));
    }

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
        m_actions.bind("debug", Key::F3);   // physics debug view
        m_actions.bind("stats", Key::F2);   // FPS and render statistics in the log
        m_actions.bind("overlay", Key::F1); // the ImGui debug overlay
        m_actions.bind("quit", Key::Escape);
    }

    // The F1 overlay: the engine's frame graph, profiler and entity inspector, plus the game's
    // switches, its state and inspectors for its own components.
    void setUpOverlay()
    {
        m_overlay.setScene(&m_scene);
        m_overlay.addToggle("Physics debug view (F3)", &m_showPhysics);
        m_overlay.addToggle("  bounding boxes", &m_physicsDebug.boundingBoxes);
        m_overlay.addToggle("  contacts", &m_physicsDebug.contacts);
        m_overlay.addToggle("  velocities", &m_physicsDebug.velocities);
        m_overlay.addToggle("Frame statistics in the log (F2)", &m_logStats);
        m_overlay.addPanel("Game", [this] {
            ImGui::SliderInt("Hearts", &m_hearts, 1, kMaxHearts);
            const int shown = m_levelIndex + 1;
            int level = shown;
            if (ImGui::SliderInt("Level", &level, 1, static_cast<int>(m_levels.size())) &&
                level != shown) {
                startLevel(level - 1);
                m_state = State::Playing;
            }
            if (ImGui::Button("Restart level")) {
                startLevel(m_levelIndex);
                m_state = State::Playing;
            }
        });

        astra2d::EntityInspector& inspector = m_overlay.inspector();
        inspector.addInspector<Player>("Player", [](Player& player) {
            ImGui::BeginDisabled(); // decided by the controller every step
            ImGui::Checkbox("Grounded", &player.grounded);
            ImGui::EndDisabled();
            ImGui::DragFloat("Invulnerable (s)", &player.invulnerable, 0.01F, 0.0F, 10.0F);
            ImGui::DragFloat("Drop time (s)", &player.dropTime, 0.01F, 0.0F, 1.0F);
        });
        inspector.addInspector<Walker>("Walker", [](Walker& walker) {
            ImGui::DragFloat("Speed", &walker.speed, 0.5F, 0.0F, 400.0F);
            ImGui::SliderFloat("Direction", &walker.direction, -1.0F, 1.0F);
        });
        inspector.addInspector<Flyer>(
            "Flyer", [](Flyer& flyer) { ImGui::DragFloat2("Home", &flyer.home.x); });
        inspector.addInspector<Mover>("Mover", [](Mover& mover) {
            ImGui::DragFloat2("Origin", &mover.origin.x);
            ImGui::DragFloat2("Travel", &mover.travel.x);
        });
        inspector.addInspector<Checkpoint>("Checkpoint", [](Checkpoint& checkpoint) {
            ImGui::Checkbox("Active", &checkpoint.active);
        });
        const auto tag = [](auto&) { ImGui::TextUnformatted("(a tag without data)"); };
        inspector.addInspector<Enemy>(
            "Enemy", [](Enemy& enemy) { ImGui::Checkbox("Stompable", &enemy.stompable); });
        inspector.addInspector<Coin>("Coin", tag);
        inspector.addInspector<HeartPickup>("Heart pickup", tag);
        inspector.addInspector<Spring>("Spring", tag);
        inspector.addInspector<HazardTile>("Hazard", tag);
    }

    // --- Systems: the gameplay, run by the Scene in this order -------------------------------

    void addSystems()
    {
        // The "physics" system was added when m_physics was made, so it runs first in every
        // fixed step and its event handlers (connectPhysicsEvents) run inside it. The systems
        // below react to where it left the bodies; the velocities they set move the bodies in the
        // next step.
        // Movers first, so the player is carried at the speed the platform moves next.
        m_scene.addSystem(SystemPhase::FixedUpdate, "movers", moveMovers);
        m_scene.addSystem(SystemPhase::FixedUpdate, "player control",
                          [this](Scene&, float dt) { controlPlayer(dt); });
        m_scene.addSystem(SystemPhase::FixedUpdate, "walkers",
                          [this](Scene& scene, float) { moveWalkers(scene); });
        m_scene.addSystem(SystemPhase::FixedUpdate, "flyers",
                          [this](Scene& scene, float dt) { moveFlyers(scene, dt); });
        m_scene.addSystem(SystemPhase::FixedUpdate, "dangers",
                          [this](Scene&, float dt) { checkDangers(dt); });

        m_scene.addSystem(SystemPhase::Update, "player look",
                          [this](Scene& scene, float) { animatePlayer(scene); });
        // The built-in "animation" system (LateUpdate) then advances every Animator.
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
        bool jumpPressed = m_actions.isPressed("jump", in); // since the previous step

        // Standing on a moving platform carries the player along with it.
        const auto below = m_physics.rayCast(feet(), {0.0F, -3.0F}, kPlatformGroup);
        glm::vec2 carry{0.0F};
        if (below && registry.has<Mover>(below->entity)) {
            carry = registry.get<astra2d::RigidBody>(below->entity).velocity;
        }
        // On a rising lift the player moves up too, and still counts as standing. The tolerance
        // covers a lift slowing down: its speed for the next step is a little below the player's.
        player.grounded =
            m_physics.isGrounded(m_player) && body.velocity.y <= std::max(carry.y, 0.0F) + 4.0F;
        if (player.grounded && !player.wasGrounded && player.lastVelocityY < kLandingSpeed) {
            spawnEffect(registry.get<Transform>(m_player).position, m_dustAnimation);
        }
        player.wasGrounded = player.grounded;
        if (jumpPressed && down && player.grounded && below) {
            player.dropTime = kDropTime; // drop through instead of jumping
            jumpPressed = false;
        }
        player.dropTime = std::max(player.dropTime - dt, 0.0F);
        // Planks are ignored while dropping; enemies while blinking after a hit.
        std::uint16_t mask = kPlayerMask;
        if (player.dropTime > 0.0F) {
            mask = astra2d::withoutGroups(mask, kPlatformGroup);
        }
        if (player.invulnerable > 0.0F) {
            mask = astra2d::withoutGroups(mask, kEnemyGroup);
        }
        collider.mask = mask; // applied by the physics only when it changes

        const bool jumping = player.jump.update(dt, player.grounded, jumpPressed);
        // The platform's speed is added on top of the player's own, which accelerates as usual.
        body.velocity.x = horizontalVelocity(body.velocity.x - player.carryX, moveX,
                                             player.grounded, m_tuning, dt) +
                          carry.x;
        player.carryX = carry.x;
        player.launched = player.launched && body.velocity.y > 0.0F;
        body.velocity.y = verticalVelocity(
            body.velocity.y, jumping, m_actions.isHeld("jump", in) || player.launched, m_tuning);
        body.gravityScale = body.velocity.y < 0.0F ? m_tuning.fallGravityScale : 1.0F;
        player.lastVelocityY = body.velocity.y;
        if (moveX != 0.0F) {
            player.facingLeft = moveX < 0.0F;
        }
        if (jumping) {
            playSound(*m_sounds.jump, 0.5F);
            if (player.grounded) {
                spawnEffect(registry.get<Transform>(m_player).position, m_dustAnimation);
            }
        }
    }

    // Just below the bottom of the player's box.
    [[nodiscard]] glm::vec2 feet() const
    {
        return m_scene.registry().get<Transform>(m_player).position - glm::vec2{0.0F, 7.5F};
    }

    // Moves each platform along a smooth back-and-forth path. A kinematic body moves only by its
    // velocity, so the velocity is the one that reaches the next point of the path in one step.
    static void moveMovers(Scene& scene, float dt)
    {
        scene.registry().view<Mover, astra2d::RigidBody, Transform>().each(
            [dt](Mover& mover, astra2d::RigidBody& body, const Transform& transform) {
            mover.time += dt;
            const float phase =
                0.5F - (0.5F * std::cos(2.0F * glm::pi<float>() * mover.time / kMoverPeriod));
            body.velocity = (mover.origin + (mover.travel * phase) - transform.position) / dt;
        });
    }

    void moveWalkers(Scene& scene)
    {
        // Walkers go until a wall or the end of their floor, then turn around. Rays look ahead:
        // one forward at body height, one down just in front of the feet.
        scene.registry()
            .view<Walker, astra2d::RigidBody, Transform, astra2d::SpriteRenderer>()
            .each([&](Entity entity, Walker& walker, astra2d::RigidBody& body,
                      const Transform& transform, astra2d::SpriteRenderer& sprite) {
            const glm::vec2 p = transform.position;
            const float d = walker.direction;
            if (m_physics.isGrounded(entity)) {
                const bool wall =
                    m_physics.rayCast(p + glm::vec2{0.0F, -3.0F}, {d * 8.0F, 0.0F}, kWorldGroup)
                        .has_value();
                const bool floor =
                    m_physics.rayCast(p + glm::vec2{d * 7.0F, -4.0F}, {0.0F, -10.0F}, kGround)
                        .has_value();
                if (wall || !floor) {
                    walker.direction = -d;
                }
            }
            body.velocity.x = walker.direction * walker.speed;
            sprite.flipX = walker.direction < 0.0F; // the art faces right
            if (p.y < kFallLimit) {
                scene.destroyLater(entity);
            }
        });
    }

    void moveFlyers(Scene& scene, float dt)
    {
        // Flyers are kinematic: they go through nothing but turn at walls and at the end of their
        // range, and bob on a sine wave.
        constexpr float kBobSpeed = 3.0F; // radians per second
        scene.registry().view<Flyer, astra2d::RigidBody, Transform>().each(
            [&](Flyer& flyer, astra2d::RigidBody& body, const Transform& transform) {
            flyer.time += dt;
            const float offset = transform.position.x - flyer.home.x;
            const bool wall =
                m_physics.rayCast(transform.position, {flyer.direction * 9.0F, 0.0F}, kWorldGroup)
                    .has_value();
            if (wall || (std::abs(offset) > kFlyerRange && offset * flyer.direction > 0.0F)) {
                flyer.direction = -flyer.direction;
            }
            body.velocity = {flyer.direction * kFlyerSpeed,
                             kFlyerBob * kBobSpeed * std::cos(kBobSpeed * flyer.time)};
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
            if (player.invulnerable > kInvulnerableTime - kHurtPoseTime) {
                animator.player.play(m_hurtAnimation);
            } else if (player.grounded) {
                // Running relative to the platform stood on, not to the world.
                animator.player.play(std::abs(body.velocity.x - player.carryX) > 8.0F
                                         ? m_runAnimation
                                         : m_idleAnimation);
            } else {
                animator.player.play(body.velocity.y > 0.0F ? m_jumpAnimation : m_fallAnimation);
            }
            sprite.flipX = player.facingLeft;
            // Blink while invulnerable; hidden once inside the rocket.
            sprite.visible = m_state != State::LevelClear && m_state != State::Victory &&
                             (player.invulnerable <= 0.0F || std::fmod(m_time, 0.16F) < 0.08F);
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

    // The player touched an enemy: from above it is stomped (unless it is spiky), otherwise the
    // player is hurt.
    void onCollision(const astra2d::CollisionEvent& event)
    {
        auto& registry = m_scene.registry();
        if (m_state != State::Playing || (event.a != m_player && event.b != m_player)) {
            return;
        }
        const Entity other = event.other(m_player);
        const auto* enemy = registry.tryGet<Enemy>(other);
        if (enemy == nullptr) {
            return;
        }
        // The normal points from a to b; turned to point from the enemy to the player.
        const glm::vec2 fromEnemy = event.a == other ? event.normal : -event.normal;
        if (fromEnemy.y > 0.5F && enemy->stompable) {
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
            spawnEffect(position, m_sparkleAnimation);
        } else if (registry.has<HeartPickup>(thing)) {
            registry.remove<HeartPickup>(thing);
            m_scene.destroyLater(thing);
            m_hearts = std::min(m_hearts + 1, kMaxHearts);
            m_audio.play(*m_sounds.coin, {.volume = 0.5F, .pitch = 0.75F});
            spawnEffect(position, m_sparkleAnimation);
        } else if (registry.has<Spring>(thing)) {
            launch(thing);
        } else if (registry.has<HazardTile>(thing)) {
            ++m_hazardContacts;
        } else if (auto* checkpoint = registry.tryGet<Checkpoint>(thing);
                   checkpoint != nullptr && !checkpoint->active) {
            checkpoint->active = true;
            registry.get<astra2d::Animator>(thing).player.play(m_checkpointOnAnimation);
            m_respawn = position;
            playSound(*m_sounds.checkpoint, 0.6F);
            spawnEffect(position + glm::vec2{0.0F, 6.0F}, m_sparkleAnimation);
        } else if (registry.has<Goal>(thing)) {
            // The player boards the rocket (and is hidden), which lifts off to the next planet.
            m_state = State::LevelClear;
            m_stateTime = kLevelClearTime;
            m_rocket = thing;
            m_totalCoins += m_coins;
            m_totalTime += m_levelTime;
            m_audio.play(*m_sounds.win, {.volume = 0.6F});
            spawnEffect(position + glm::vec2{0.0F, 8.0F}, m_sparkleAnimation);
        }
    }

    // Moves the boarded rocket @p time seconds into its lift-off (nothing before 0).
    // The rocket @p time seconds after it started, in a frame of @p dt seconds.
    void liftOff(float time, float dt)
    {
        auto& registry = m_scene.registry();
        const auto* goal = registry.tryGet<Goal>(m_rocket);
        if (goal == nullptr || time <= 0.0F) {
            return;
        }
        auto& exhaust = registry.get<astra2d::SpriteRenderer>(goal->exhaust);
        if (!exhaust.visible) { // the first frame of the lift-off
            exhaust.visible = true;
            spawnEffect(goal->base, m_dustAnimation);
            playSound(*m_sounds.stomp, 0.8F);
        }
        m_shake.addTrauma(time < 1.0F ? 0.03F : 0.0F);
        const glm::vec2 at = goal->base + glm::vec2{0.0F, 0.5F * kLiftOffAccel * time * time};
        registry.get<Transform>(m_rocket).position = at;
        registry.get<Transform>(goal->top).position = at + glm::vec2{0.0F, kRocketTopOffset};
        registry.get<Transform>(goal->exhaust).position = at - glm::vec2{0.0F, kExhaustOffset};
        m_smoke.stream(at - glm::vec2{0.0F, kExhaustOffset + 6.0F}, 40.0F, dt);
    }

    void stomp(Entity enemy)
    {
        auto& registry = m_scene.registry();
        // Without a body and collider it is no longer simulated; it stays squashed for a moment.
        const bool flyer = registry.remove<Flyer>(enemy);
        registry.remove<Walker>(enemy);
        registry.remove<Enemy>(enemy);
        registry.remove<astra2d::RigidBody>(enemy);
        registry.remove<astra2d::Collider>(enemy);
        registry.emplace<astra2d::Lifetime>(enemy, astra2d::Lifetime{.duration = kSquashTime});
        registry.get<astra2d::Animator>(enemy).player.play(flyer ? m_stunnedAnimation
                                                                 : m_squashAnimation);

        auto& player = registry.get<Player>(m_player);
        auto& body = registry.get<astra2d::RigidBody>(m_player);
        // Holding jump bounces as high as a jump.
        body.velocity.y =
            m_actions.isHeld("jump", input()) ? m_tuning.jumpSpeed : m_tuning.stompBounce;
        player.jump.reset();
        playSound(*m_sounds.stomp, 0.7F);
        m_shake.addTrauma(0.25F);
        spawnEffect(registry.get<Transform>(enemy).position, m_sparkleAnimation);
    }

    // The player landed on (or walked into) a spring: thrown up high.
    void launch(Entity spring)
    {
        auto& registry = m_scene.registry();
        auto& body = registry.get<astra2d::RigidBody>(m_player);
        if (body.velocity.y > 0.0F) {
            return; // on the way up already, e.g. jumping past it
        }
        auto& player = registry.get<Player>(m_player);
        body.velocity.y = kSpringSpeed;
        player.launched = true;
        player.jump.reset();
        registry.get<astra2d::Animator>(spring).player.play(m_springAnimation, true);
        m_audio.play(*m_sounds.jump, {.volume = 0.6F, .pitch = 0.7F});
        spawnEffect(registry.get<Transform>(spring).position, m_dustAnimation);
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
        // Each planet has its own weather.
        m_weather.resize(static_cast<std::size_t>(currentLook().weather.count));
        std::uniform_real_distribution<float> unit(0.0F, 1.0F);
        for (glm::vec3& particle : m_weather) { // x, y in a view-sized box; z: a phase
            particle = {unit(m_random) * 400.0F, unit(m_random) * kViewHeight,
                        unit(m_random) * 6.28F};
        }
        window().input().clearFixedStepPresses(); // e.g. the Space that confirmed a menu
        m_shake.reset();
        m_smoke.clear();
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
            spawn(thing);
        }
        m_follow.setBounds({0.0F, 0.0F}, m_level.size());
        followPlayer(0.0F, true);
    }

    void spawnPlayer(glm::vec2 position)
    {
        m_player = m_scene.createEntity("player", position);
        m_respawn = position;
        auto& registry = m_scene.registry();
        registry.emplace<Player>(m_player);
        registry.emplace<astra2d::SpriteRenderer>(
            m_player, astra2d::SpriteRenderer{.size = kBigSize, .layer = 3});
        registry.emplace<astra2d::Animator>(m_player, astra2d::Animator{.sheet = &m_big})
            .player.play(m_idleAnimation);
        // A box around the astronaut's art (12x18 of the 24x24 frame, from the boots to the top
        // of the helmet), without friction so it slides along walls; the controller sets its
        // velocity directly.
        registry.emplace<astra2d::RigidBody>(
            m_player, astra2d::RigidBody{.fixedRotation = true, .allowSleep = false});
        registry.emplace<astra2d::Collider>(m_player, astra2d::Collider{.halfExtents = {6.0F, 9.0F},
                                                                        .offset = {0.0F, 1.0F},
                                                                        .friction = 0.0F,
                                                                        .category = kPlayerGroup,
                                                                        .mask = kPlayerMask});
        registry.emplace<astra2d::Interpolated>(m_player);
    }

    void spawn(const Thing& thing)
    {
        auto& registry = m_scene.registry();
        const glm::vec2 at = thing.position;
        switch (thing.kind) {
        case ThingKind::Player:
            spawnPlayer(at);
            break;
        case ThingKind::Coin:
            registry.emplace<Coin>(spawnAnimated("coin", at, m_spinAnimation, 1));
            break;
        case ThingKind::Heart:
            registry.emplace<HeartPickup>(spawnAnimated("heart", at, m_heartAnimation, 1));
            break;
        case ThingKind::Crawler:
            spawnWalker(spawnAnimated("crawler", at, m_crawlAnimation, 2), kCrawlerSpeed,
                        {6.0F, 4.5F}, true);
            break;
        case ThingKind::Spiker:
            spawnWalker(spawnAnimated("spiker", at, m_spikerAnimation, 2), kSpikerSpeed,
                        {6.0F, 5.0F}, false);
            break;
        case ThingKind::Flyer:
            spawnFlyer(at);
            break;
        case ThingKind::Spring: {
            const Entity spring = spawnAnimated("spring", at, m_springIdleAnimation, 1);
            registry.emplace<Spring>(spring);
            registry.get<astra2d::Collider>(spring) = trigger({6.0F, 4.0F}, {0.0F, -3.0F});
            break;
        }
        case ThingKind::Platform:
            spawnMover(at, kPlatformTravel);
            break;
        case ThingKind::Lift:
            spawnMover(at, kLiftTravel);
            break;
        case ThingKind::Checkpoint: {
            const Entity checkpoint = spawnAnimated("checkpoint", at, m_checkpointAnimation, 1);
            registry.emplace<Checkpoint>(checkpoint);
            registry.get<astra2d::Collider>(checkpoint) = trigger({5.0F, 8.0F});
            break;
        }
        case ThingKind::Goal: {
            const Entity goal = spawnAnimated("rocket", at, m_rocketAnimation, 1, &m_big);
            const Entity top = spawnAnimated("rocket top", at + glm::vec2{0.0F, kRocketTopOffset},
                                             m_rocketTopAnimation, 1, &m_big);
            const Entity exhaust = spawnAnimated("exhaust", at, m_exhaustAnimation, 1, &m_big);
            registry.remove<astra2d::Collider>(top);
            registry.remove<astra2d::Collider>(exhaust);
            registry.get<astra2d::SpriteRenderer>(exhaust).visible = false;
            registry.emplace<Goal>(goal, Goal{.base = at, .top = top, .exhaust = exhaust});
            registry.get<astra2d::Collider>(goal) = trigger({5.0F, 8.0F});
            break;
        }
        }
    }

    // An entity with a sprite playing @p animation from @p sheet (platformer.png unless given) and,
    // for pickups, a small trigger (changed or replaced by the caller where needed).
    Entity spawnAnimated(std::string_view name, glm::vec2 position,
                         const astra2d::SpriteAnimation& animation, int layer,
                         const astra2d::SpriteSheet* sheet = nullptr)
    {
        const Entity entity = m_scene.createEntity(name, position);
        auto& registry = m_scene.registry();
        const bool big = sheet == &m_big;
        registry.emplace<astra2d::SpriteRenderer>(
            entity, astra2d::SpriteRenderer{.size = big ? kBigSize : kSpriteSize, .layer = layer});
        registry
            .emplace<astra2d::Animator>(
                entity, astra2d::Animator{.sheet = sheet != nullptr ? sheet : &m_sheet})
            .player.play(animation);
        registry.emplace<astra2d::Collider>(entity, trigger({5.0F, 6.0F}));
        return entity;
    }

    void spawnWalker(Entity entity, float speed, glm::vec2 halfExtents, bool stompable)
    {
        auto& registry = m_scene.registry();
        registry.emplace<Enemy>(entity, Enemy{.stompable = stompable});
        registry.emplace<Walker>(entity, Walker{.speed = speed});
        registry.emplace<astra2d::RigidBody>(
            entity, astra2d::RigidBody{.fixedRotation = true, .allowSleep = false});
        // Feet at the bottom of the tile. Enemies pass through each other, so two never get stuck
        // turning back and forth.
        registry.get<astra2d::Collider>(entity) =
            astra2d::Collider{.halfExtents = halfExtents,
                              .offset = {0.0F, halfExtents.y - 8.0F},
                              .friction = 0.0F,
                              .category = kEnemyGroup,
                              .mask = astra2d::collisionGroups(kGround, kPlayerGroup)};
        registry.emplace<astra2d::Interpolated>(entity);
    }

    void spawnFlyer(glm::vec2 position)
    {
        const Entity entity = spawnAnimated("bat", position, m_flyAnimation, 2);
        auto& registry = m_scene.registry();
        registry.emplace<Enemy>(entity);
        registry.emplace<Flyer>(entity, Flyer{.home = position});
        registry.emplace<astra2d::RigidBody>(
            entity, astra2d::RigidBody{.type = astra2d::BodyType::Kinematic, .allowSleep = false});
        registry.get<astra2d::Collider>(entity) = astra2d::Collider{
            .halfExtents = {5.0F, 4.0F}, .category = kEnemyGroup, .mask = kPlayerGroup};
        registry.emplace<astra2d::Interpolated>(entity);
    }

    // A kinematic one-way platform, 3 tiles wide, in the planks' group (so the player can drop
    // through it too). Drawn by drawMovers().
    void spawnMover(glm::vec2 position, glm::vec2 travel)
    {
        const Entity entity = m_scene.createEntity("mover", position);
        auto& registry = m_scene.registry();
        registry.emplace<Mover>(entity, Mover{.origin = position, .travel = travel});
        registry.emplace<astra2d::RigidBody>(
            entity, astra2d::RigidBody{.type = astra2d::BodyType::Kinematic, .allowSleep = false});
        // The art is the top 7 rows of the tile.
        registry.emplace<astra2d::Collider>(entity, astra2d::Collider{.halfExtents = {24.0F, 3.5F},
                                                                      .offset = {0.0F, 4.5F},
                                                                      .friction = 0.0F,
                                                                      .oneWay = true,
                                                                      .category = kPlatformGroup,
                                                                      .mask = kPlayerGroup});
        registry.emplace<astra2d::Interpolated>(entity);
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

    // A one-shot animation (sparkle, dust) that the Scene removes when it ends.
    void spawnEffect(glm::vec2 position, const astra2d::SpriteAnimation& animation)
    {
        const Entity effect = m_scene.createEntity({}, position);
        auto& registry = m_scene.registry();
        registry.emplace<astra2d::SpriteRenderer>(
            effect, astra2d::SpriteRenderer{.size = kSpriteSize, .layer = 4});
        registry
            .emplace<astra2d::Animator>(
                effect, astra2d::Animator{.sheet = &m_sheet, .destroyWhenFinished = true})
            .player.play(animation);
    }

    // A static trigger box that only the player sets off.
    static astra2d::Collider trigger(glm::vec2 halfExtents, glm::vec2 offset = {},
                                     std::uint16_t category = kPickupGroup)
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
        m_audio.play(sound, {.volume = volume, .pitchVariation = 0.06F});
    }

    // --- Camera and drawing -------------------------------------------------------------------

    // Moves the camera towards the player (at once if @p snap), a little ahead in the direction
    // they face; m_follow keeps it inside the level.
    void followPlayer(float dt, bool snap)
    {
        const auto& registry = m_scene.registry();
        if (!registry.valid(m_player)) {
            return;
        }
        const glm::vec2 player = m_scene.worldTransform(m_player, interpolationAlpha()).position;
        const bool left = registry.get<Player>(m_player).facingLeft;
        const glm::vec2 target = player + glm::vec2{left ? -kLookAhead : kLookAhead, 16.0F};
        if (snap) {
            m_follow.snap(target, m_camera);
        } else {
            m_follow.update(target, dt, m_camera);
        }
    }

    // The sky as horizontal bands from skyTop to skyBottom, in screen space (a stepped gradient
    // that suits the pixel art), then the nebula, the stars, the planet's rings and the planet in
    // it.
    void drawSky(const Look& look)
    {
        constexpr int kBands = 24;
        const glm::vec2 screen(framebufferSize());
        const float band = std::ceil(screen.y / kBands);
        const float scale = m_camera.pixelsPerUnit();
        const auto screenSpace = [&] {
            m_renderer.begin(
                astra2d::OrthographicCamera::screenSpace(framebufferSize().x, framebufferSize().y));
        };
        screenSpace();
        for (int i = 0; i < kBands; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kBands - 1);
            const float top = screen.y - (band * static_cast<float>(i));
            m_renderer.drawQuad({screen.x * 0.5F, top - (band * 0.5F)}, {screen.x, band},
                                glm::mix(look.skyTop, look.skyBottom, t));
        }
        m_renderer.end();
        if (look.nebula.a > 0.0F) {
            Layer nebula = kNebula;
            nebula.tint = look.nebula;
            drawBackgroundLayer(nebula);
        }
        if (look.stars) {
            drawBackgroundLayer(kStars);
        }
        screenSpace();
        if (look.stars) {
            drawShootingStar(screen, scale);
        }
        // Far away things drift only a little as the camera moves.
        const float drift = m_follow.position().x * 0.03F * scale;
        if (look.rings) {
            const astra2d::Texture2D& rings = m_assets.texture("textures/rings.png");
            const glm::vec2 size = glm::vec2(rings.size()) * scale;
            m_renderer.drawQuad(glm::round(glm::vec2{(screen.x * 0.5F) - (drift * 0.5F),
                                                     screen.y - (size.y * 0.32F)}),
                                size, rings, {1.0F, 1.0F, 1.0F, 0.85F});
        }
        if (look.skyPlanet != planet::kNone) {
            // The next stop of the route.
            const glm::vec2 at{(screen.x * 0.72F) - drift, screen.y * 0.72F};
            m_renderer.drawSprite(glm::round(at), glm::vec2{1.5F * kPlanetSize * scale},
                                  m_planets.frame(look.skyPlanet));
        }
        m_renderer.end();
    }

    // Every few seconds a shooting star streaks across the sky, somewhere else each time; drawn
    // as texel-sized squares that fade along its tail. Inside a screen-space begin()/end().
    void drawShootingStar(glm::vec2 screen, float scale)
    {
        constexpr float kPeriod = 5.0F;   // seconds from one to the next
        constexpr float kDuration = 0.7F; // seconds one is seen
        constexpr int kTail = 14;         // texels
        const float cycle = std::floor(m_time / kPeriod);
        const float t = (m_time - (cycle * kPeriod)) / kDuration;
        if (t >= 1.0F) {
            return;
        }
        const auto random = [cycle](float seed) { // the same for the whole cycle
            const float value = std::sin((cycle * 12.9898F) + (seed * 78.233F)) * 43758.547F;
            return value - std::floor(value);
        };
        const glm::vec2 start{screen.x * (0.3F + (0.6F * random(1.0F))),
                              screen.y * (0.65F + (0.3F * random(2.0F)))};
        const glm::vec2 direction = glm::normalize(glm::vec2{-1.0F, -0.45F});
        const glm::vec2 head = start + (direction * t * 160.0F * scale);
        const float fade = std::min(1.0F, 4.0F * (1.0F - t));
        for (int i = 0; i < kTail; ++i) {
            const float along = static_cast<float>(i) / static_cast<float>(kTail);
            m_renderer.drawQuad(glm::round(head - (direction * static_cast<float>(i) * scale)),
                                glm::vec2{scale}, {1.0F, 0.95F, 0.85F, fade * (1.0F - along)});
        }
    }

    // An endless strip of a tiling texture that scrolls slower than the world (parallax), filled
    // below with its fill colour; or, for a layer that fills the view, the texture everywhere.
    void drawBackgroundLayer(const Layer& layer)
    {
        const astra2d::Texture2D& texture = m_assets.texture(layer.texture, kRepeat);
        const astra2d::OrthographicCamera camera = m_camera.withParallax(layer.parallax);
        const glm::vec2 min = camera.visibleMin();
        const glm::vec2 max = camera.visibleMax();
        const glm::vec2 tile(texture.size());
        const float width = max.x - min.x;
        const float centerX = (min.x + max.x) * 0.5F;
        m_renderer.begin(camera);
        if (layer.fillsView) {
            m_renderer.drawTiled((min + max) * 0.5F, max - min, texture, tile, layer.tint);
        } else {
            // Half a texel short at the top: the texture repeats vertically too, and its top edge
            // could otherwise sample the opposite (bottom) row, a dark line across the sky.
            const float bottom = layer.y - (tile.y * 0.5F);
            const float height = tile.y - 0.5F;
            m_renderer.drawTiled({centerX, bottom + (height * 0.5F)}, {width, height}, texture,
                                 tile, layer.tint, {0.0F, bottom});
            if (layer.fill.a > 0.0F && bottom > min.y) {
                m_renderer.drawQuad({centerX, (bottom + min.y) * 0.5F}, {width, bottom - min.y},
                                    layer.fill);
            }
        }
        m_renderer.end();
    }

    // Moving platforms are three sprites wide, so they are drawn here rather than by the scene.
    void drawMovers()
    {
        const float alpha = interpolationAlpha();
        m_scene.registry().view<Mover>().each([&](Entity entity, const Mover&) {
            const glm::vec2 center = m_scene.worldTransform(entity, alpha).position;
            for (int i = 0; i < 3; ++i) {
                m_renderer.drawSprite(center +
                                          glm::vec2{kTileSize * static_cast<float>(i - 1), 0.0F},
                                      kSpriteSize, m_sheet.frame(kPlatformLeft + i));
            }
        });
    }

    [[nodiscard]] const Look& currentLook() const
    {
        return kLooks[static_cast<std::size_t>(m_levelIndex) % kLooks.size()];
    }

    void driftWeather(float dt)
    {
        const Weather& weather = currentLook().weather;
        for (glm::vec3& particle : m_weather) {
            particle.x +=
                (weather.velocity.x + (weather.sway * std::sin((m_time * 1.3F) + particle.z))) * dt;
            particle.y += weather.velocity.y * dt;
        }
    }

    // The weather particles, wrapped into the camera's view so they fill it wherever it goes.
    void drawWeather(const Weather& weather)
    {
        const glm::vec2 min = m_camera.visibleMin();
        const glm::vec2 size = m_camera.visibleSize();
        const auto wrap = [](float value, float range) {
            const float r = std::fmod(value, range);
            return r < 0.0F ? r + range : r;
        };
        for (const glm::vec3& particle : m_weather) {
            glm::vec4 color = weather.color;
            if (weather.flicker) {
                color.a *= 0.55F + (0.45F * std::sin((m_time * 4.0F) + (particle.z * 3.0F)));
            }
            const glm::vec2 at =
                min + glm::vec2{wrap(particle.x - min.x, size.x), wrap(particle.y - min.y, size.y)};
            m_renderer.drawQuad(at, glm::vec2{weather.size}, color);
        }
    }

    // --- HUD, in screen pixels ----------------------------------------------------------------

    void drawText(std::string_view text, glm::vec2 topLeft, float size, glm::vec4 color)
    {
        const float shadow = std::max(1.0F, size / 8.0F);
        m_renderer.drawText(m_font, text, topLeft,
                            {.size = size,
                             .color = color,
                             .shadowOffset = {shadow, -shadow},
                             .shadowColor = kShadow});
    }

    // Centred text, made smaller (in whole steps of the 8 pixel font, so it stays crisp) if it
    // does not fit the window's width.
    void drawCentered(std::string_view text, float y, float size, glm::vec4 color)
    {
        const float width = static_cast<float>(framebufferSize().x);
        const float fits = 0.94F * width / m_font.atlas().measure(text, 1.0F).x;
        size = std::max(8.0F, std::min(size, std::floor(fits / 8.0F) * 8.0F));
        const float shadow = std::max(1.0F, size / 8.0F);
        m_renderer.drawText(m_font, text, {std::round(width * 0.5F), y},
                            {.size = size,
                             .color = color,
                             .anchor = {0.5F, 1.0F},
                             .shadowOffset = {shadow, -shadow},
                             .shadowColor = kShadow});
    }

    void drawHud()
    {
        const glm::vec2 screen(framebufferSize());
        const float s = hudScale(screen);
        const float text = 8.0F * s;
        m_renderer.begin(
            astra2d::OrthographicCamera::screenSpace(framebufferSize().x, framebufferSize().y));
        if (m_state != State::Title && m_state != State::Victory) {
            drawStatusBar(screen, s);
        }
        const float middle = screen.y * 0.5F;
        switch (m_state) {
        case State::Title:
            dim(screen, 0.25F);
            drawRoute(screen, middle + (86.0F * s), s);
            panel(screen, 196.0F * s, middle - (80.0F * s), middle + (60.0F * s), s);
            drawTitleArt(screen, middle + (14.0F * s), s);
            drawCentered("ASTRA RUN", middle + (48.0F * s), 32.0F * s, kGold);
            drawCentered("a stranded astronaut, four planets, one way home", middle, text, kWhite);
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
                const float top = screen.y - (36.0F * s);
                drawCentered(m_level.name, top - (16.0F * s), 16.0F * s,
                             {kGold.r, kGold.g, kGold.b, alpha});
                drawCentered(m_level.hint, top - (40.0F * s), text, {1.0F, 1.0F, 1.0F, alpha});
            }
            break;
        case State::Paused:
            dim(screen, 0.4F);
            panel(screen, 150.0F * s, middle - (34.0F * s), middle + (18.0F * s), s);
            drawCentered("PAUSED", middle + (8.0F * s), 16.0F * s, kWhite);
            drawCentered("P or ENTER to continue", middle - (16.0F * s), text, kWhite);
            break;
        case State::LevelClear: {
            if (kLevelClearTime - m_stateTime < kLiftOffPanelTime) {
                break; // watching the rocket go
            }
            // The next planet of the route; after the last level, Earth.
            const int next = m_levelIndex + 1;
            const std::string_view nextName =
                std::cmp_less(next, m_levels.size())
                    ? std::string_view(m_levels[static_cast<std::size_t>(next)].name)
                    : "Earth";
            panel(screen, 150.0F * s, middle - (52.0F * s), middle + (40.0F * s), s);
            drawCentered("LIFT OFF!", middle + (30.0F * s), 16.0F * s, kGold);
            m_renderer.drawSprite({std::round(screen.x * 0.5F), middle}, glm::vec2{kPlanetSize * s},
                                  m_planets.frame(std::min(next, planet::kEarth)));
            drawCentered(std::format("next stop: {}", nextName), middle - (22.0F * s), text,
                         kWhite);
            drawCentered(std::format("coins {}/{}", m_coins, m_level.count(ThingKind::Coin)),
                         middle - (36.0F * s), text, kGold);
            break;
        }
        case State::GameOver:
            dim(screen, 0.45F);
            panel(screen, 150.0F * s, middle - (34.0F * s), middle + (34.0F * s), s);
            drawCentered("GAME OVER", middle + (24.0F * s), 24.0F * s, kRed);
            drawCentered("ENTER to try again   ESC to quit", middle - (16.0F * s), text, kWhite);
            break;
        case State::Victory: {
            int allCoins = 0;
            for (const Level& level : m_levels) {
                allCoins += level.count(ThingKind::Coin);
            }
            const float top = screen.y;
            panel(screen, 160.0F * s, top - (92.0F * s), top - (10.0F * s), s);
            drawCentered("YOU MADE IT HOME!", top - (22.0F * s), 16.0F * s, kGold);
            drawCentered("welcome back to Earth, astronaut", top - (46.0F * s), text, kWhite);
            drawCentered(std::format("coins {}/{}   time {}:{:02}", m_totalCoins, allCoins,
                                     static_cast<int>(m_totalTime) / 60,
                                     static_cast<int>(m_totalTime) % 60),
                         top - (62.0F * s), text, kWhite);
            if (std::fmod(m_time, 1.0F) < 0.7F) {
                drawCentered("ENTER to play again", top - (78.0F * s), text, kGold);
            }
            break;
        }
        }
        m_renderer.end();
    }

    // One bar along the top: hearts and coins on the left, the level in the middle, the time on
    // the right. @p s is the HUD's pixel scale.
    void drawStatusBar(glm::vec2 screen, float s)
    {
        const float text = 8.0F * s;
        const float row = screen.y - (14.0F * s); // centre line of the bar
        const float textTop = row + (4.0F * s);
        examples::drawPanel(m_renderer, {-s, screen.y - (28.0F * s)}, {screen.x + s, screen.y + s},
                            s);

        for (int i = 0; i < kMaxHearts; ++i) {
            m_renderer.drawSprite({(14.0F + (18.0F * static_cast<float>(i))) * s, row},
                                  glm::vec2{16.0F * s},
                                  m_sheet.frame(i < m_hearts ? kHeartFull : kHeartEmpty));
        }
        const float coinX = (14.0F + (18.0F * static_cast<float>(kMaxHearts)) + 8.0F) * s;
        m_renderer.drawSprite({coinX, row}, glm::vec2{16.0F * s}, m_sheet.frame(kCoinIcon));
        const std::string coins = std::format("{}/{}", m_coins, m_level.count(ThingKind::Coin));
        drawText(coins, {coinX + (12.0F * s), textTop}, text, kGold);
        const float leftEnd = coinX + (12.0F * s) + m_font.atlas().measure(coins, text).x;

        std::string right = std::format("TIME {}:{:02}", static_cast<int>(m_levelTime) / 60,
                                        static_cast<int>(m_levelTime) % 60);
        if (m_audio.muted()) {
            right = "MUTED  " + right;
        }
        const float rightStart = screen.x - (12.0F * s) - m_font.atlas().measure(right, text).x;
        drawText(right, {rightStart, textTop}, text, kWhite);

        // The level's name too, if it fits between the two sides.
        std::string level = std::format("{} \u00b7 {}", m_levelIndex + 1, m_level.name);
        if (m_font.atlas().measure(level, text).x > rightStart - leftEnd - (24.0F * s)) {
            level = std::format("LEVEL {}", m_levelIndex + 1);
        }
        const float width = m_font.atlas().measure(level, text).x; // centred in the free space
        drawText(level, {std::round((leftEnd + rightStart - width) * 0.5F), textTop}, text, kWhite);
    }

    // The astronaut (twice the HUD's scale, bobbing) left of the title and their rocket right of
    // it, both standing on @p ground.
    void drawTitleArt(glm::vec2 screen, float ground, float s)
    {
        const float x = 170.0F * s;
        const float bob = std::round(std::sin(m_time * 2.0F) * 1.5F) * s;
        const int idle = kAstronautIdle + (static_cast<int>(m_time * 2.0F) % 2);
        // A frame's art stands on row 19 of 24, 8 texels below its centre.
        m_renderer.drawSprite({std::round((screen.x * 0.5F) - x), ground + (16.0F * s) + bob},
                              kBigSize * 2.0F * s, m_big.frame(idle));
        const glm::vec2 rocket{std::round((screen.x * 0.5F) + x), ground + (8.0F * s)};
        m_renderer.drawSprite(rocket, kBigSize * s, m_big.frame(kRocketBottom));
        m_renderer.drawSprite(rocket + glm::vec2{0.0F, kRocketTopOffset * s}, kBigSize * s,
                              m_big.frame(kRocketTop));
    }

    // The route across the top: every planet, linked by a dotted line, with Earth at the end.
    void drawRoute(glm::vec2 screen, float y, float s)
    {
        const float step = 44.0F * s;
        const float first = (screen.x * 0.5F) - (step * (planet::kCount - 1) * 0.5F);
        for (int i = 0; i + 1 < planet::kCount; ++i) { // the dots between two planets
            for (int dot = 1; dot < 4; ++dot) {
                const float x =
                    first + (step * (static_cast<float>(i) + (0.25F * static_cast<float>(dot))));
                m_renderer.drawQuad({std::round(x), y}, glm::vec2{2.0F * s},
                                    {1.0F, 1.0F, 1.0F, 0.6F});
            }
        }
        for (int i = 0; i < planet::kCount; ++i) {
            m_renderer.drawSprite({std::round(first + (step * static_cast<float>(i))), y},
                                  glm::vec2{kPlanetSize * s}, m_planets.frame(i));
        }
    }

    // A panel centred horizontally, @p halfWidth to each side (at most the window's width), from
    // @p bottom to @p top.
    void panel(glm::vec2 screen, float halfWidth, float bottom, float top, float s)
    {
        halfWidth = std::min(halfWidth, (screen.x * 0.5F) - (4.0F * s));
        examples::drawPanel(m_renderer, {(screen.x * 0.5F) - halfWidth, bottom},
                            {(screen.x * 0.5F) + halfWidth, top}, s);
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
    astra2d::SpriteSheet m_big;     // the astronaut and the rocket, 24x24 frames
    astra2d::SpriteSheet m_planets; // the route, see looks.hpp
    astra2d::Font m_font;
    astra2d::OrthographicCamera m_camera;
    astra2d::CameraShake m_shake;
    astra2d::CameraFollow m_follow{kCameraSpeed};
    // Exhaust smoke of a lifting rocket: puffs pushed down and aside that grow and fade.
    astra2d::ParticleEmitter m_smoke{{.lifetime = 1.2F,
                                      .lifetimeVariation = 0.3F,
                                      .direction = {0.0F, -1.0F},
                                      .spread = 1.4F,
                                      .speed = 45.0F,
                                      .speedVariation = 0.4F,
                                      .gravity = {0.0F, 30.0F},
                                      .damping = 1.5F,
                                      .area = {3.0F, 1.0F},
                                      .startSize = 3.0F,
                                      .endSize = 9.0F,
                                      .startColor = {0.95F, 0.85F, 0.7F, 0.8F},
                                      .endColor = {0.55F, 0.55F, 0.6F, 0.0F}}};
    astra2d::Renderer2D m_renderer;
    astra2d::ActionMap m_actions;
    std::mt19937 m_random;
    MoveTuning m_tuning;

    // Animations are data that Animators point to, so they are declared before the scene. The
    // astronaut's and the rocket's frames are in m_big, the others in m_sheet.
    const astra2d::SpriteAnimation m_idleAnimation{.frames = {0, 1}, .frameDuration = 0.5F};
    const astra2d::SpriteAnimation m_runAnimation{.frames = {2, 3, 4, 5}, .frameDuration = 0.09F};
    const astra2d::SpriteAnimation m_jumpAnimation{.frames = {6}};
    const astra2d::SpriteAnimation m_fallAnimation{.frames = {7}};
    const astra2d::SpriteAnimation m_crawlAnimation{.frames = {8, 9}, .frameDuration = 0.2F};
    const astra2d::SpriteAnimation m_squashAnimation{.frames = {10}};
    const astra2d::SpriteAnimation m_spinAnimation{.frames = {11, 12, 13, 14},
                                                   .frameDuration = 0.12F};
    const astra2d::SpriteAnimation m_rocketAnimation{.frames = {kRocketBottom, kRocketBottom + 1},
                                                     .frameDuration = 0.5F};
    const astra2d::SpriteAnimation m_rocketTopAnimation{.frames = {kRocketTop}};
    const astra2d::SpriteAnimation m_exhaustAnimation{.frames = {12, 13}, .frameDuration = 0.06F};
    const astra2d::SpriteAnimation m_sparkleAnimation{
        .frames = {kSparkle, kSparkle + 1, kSparkle + 2}, .frameDuration = 0.07F, .loop = false};
    const astra2d::SpriteAnimation m_hurtAnimation{.frames = {8}};
    const astra2d::SpriteAnimation m_heartAnimation{.frames = {kHeartFull}};
    const astra2d::SpriteAnimation m_flyAnimation{.frames = {24, 25}, .frameDuration = 0.14F};
    const astra2d::SpriteAnimation m_stunnedAnimation{.frames = {26}};
    const astra2d::SpriteAnimation m_spikerAnimation{.frames = {27, 28}, .frameDuration = 0.25F};
    const astra2d::SpriteAnimation m_springIdleAnimation{.frames = {kSpringFrame}};
    const astra2d::SpriteAnimation m_springAnimation{
        .frames = {kSpringFrame + 1, kSpringFrame + 1, kSpringFrame},
        .frameDuration = 0.07F,
        .loop = false};
    const astra2d::SpriteAnimation m_dustAnimation{
        .frames = {32, 33, 34}, .frameDuration = 0.08F, .loop = false};
    const astra2d::SpriteAnimation m_checkpointAnimation{.frames = {kCheckpointOff}};
    const astra2d::SpriteAnimation m_checkpointOnAnimation{.frames = {kCheckpointOn}};
    Scene m_scene;
    astra2d::PhysicsSystem m_physics; // after the scene: it must be destroyed first
    std::vector<astra2d::Connection> m_connections;

    std::vector<Level> m_levels;
    int m_firstLevel = 0;
    Level m_level; // the one being played
    int m_levelIndex = 0;
    Entity m_player;
    Entity m_rocket; // the goal the player boarded
    glm::vec2 m_respawn{0.0F};
    bool m_showPhysics = false;
    // Sizes in world units, where a tile is 16. Shows the merged tile boxes (inner edges faint),
    // triggers, contacts and velocities.
    astra2d::PhysicsDebugSettings m_physicsDebug{
        .lineWidth = 0.75F, .contactSize = 4.0F, .normalLength = 8.0F, .velocityScale = 0.1F};
    astra2d::DebugOverlay m_overlay{*this};
    bool m_logStats = false;
    astra2d::FrameStats m_frameStats;

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
    std::vector<glm::vec3> m_weather; // see driftWeather()
};

} // namespace

void runGame(int firstLevel)
{
    Game game(firstLevel);
    game.run();
}

} // namespace platformer
