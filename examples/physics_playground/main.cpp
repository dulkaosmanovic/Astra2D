// physics_playground: the physics module through the Scene and PhysicsSystem.
//
// Every object is an entity with a Transform, a Collider and (if it moves) a RigidBody; the
// PhysicsSystem simulates them at the fixed timestep and writes the results back. The level shows
// the features: a tile map with one-way tiles, a one-way platform, a slope, a moving (kinematic)
// platform, a trigger zone, a pyramid, and fast bullets with continuous collision.
//
// Units are metres: the defaults of PhysicsSettings suit objects of 0.1 to 10 units.
//
// Controls:
//   left click             spawn the selected shape (1 box, 2 circle, 3 polygon)
//   left drag on a body    pull it with a spring
//   right click            delete the body under the mouse
//   Space                  shoot a bullet towards the mouse
//   P / T                  build a pyramid / a tower at the mouse
//   B                      next broad phase (grid, sort and sweep, brute force)
//   D                      debug draw: outlines, bounding boxes, contacts, normals, velocities
//   F2                     log FPS, frame times and render statistics every second
//   Z                      sleeping on / off
//   Enter / N              pause / one step while paused
//   R                      reset, Escape quit

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/debug/frame_stats.hpp>
#include <astra2d/debug/physics_debug_draw.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/font.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/scene/components.hpp>
#include <astra2d/scene/physics_system.hpp>
#include <astra2d/scene/scene.hpp>

#include "../common/example_assets.hpp"
#include "../common/hud.hpp"

#include <glad/gl.h>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cmath>
#include <exception>
#include <format>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

using astra2d::Collider;
using astra2d::ColliderShape;
using astra2d::Entity;
using astra2d::RigidBody;
using astra2d::Transform;

constexpr glm::vec4 kBackground{0.07F, 0.08F, 0.11F, 1.0F};
constexpr glm::vec4 kStatic{0.35F, 0.38F, 0.45F, 1.0F};
constexpr glm::vec4 kKinematic{0.35F, 0.55F, 0.95F, 1.0F};
constexpr glm::vec4 kOneWay{0.95F, 0.6F, 0.2F, 1.0F};
constexpr glm::vec4 kTrigger{0.3F, 0.9F, 0.5F, 0.18F};
constexpr glm::vec4 kText{0.9F, 0.92F, 1.0F, 1.0F};

constexpr float kArenaHalfWidth = 20.0F;

// The game's own components.
struct Tint {
    glm::vec4 color{1.0F};
};
// Moves back and forth between two x positions (a kinematic platform).
struct Patrol {
    float minX = 0.0F;
    float maxX = 0.0F;
    float speed = 2.0F;
};

enum class SpawnShape { Box, Circle, Polygon };

constexpr std::array kBroadPhases = {astra2d::BroadPhaseType::UniformGrid,
                                     astra2d::BroadPhaseType::SortAndSweep,
                                     astra2d::BroadPhaseType::BruteForce};

std::string_view broadPhaseName(astra2d::BroadPhaseType type)
{
    switch (type) {
    case astra2d::BroadPhaseType::UniformGrid:
        return "uniform grid";
    case astra2d::BroadPhaseType::SortAndSweep:
        return "sort and sweep";
    case astra2d::BroadPhaseType::BruteForce:
        return "brute force";
    }
    return "";
}

class PhysicsPlayground : public astra2d::Application {
public:
    PhysicsPlayground()
        : Application(
              {.window = {.title = "Astra2D: physics playground", .width = 1280, .height = 720}})
        , m_assets(examples::assetsDirectory())
        , m_font(astra2d::Font::fromFile(m_assets.resolve("fonts/PressStart2P-Regular.ttf"), 8.0F))
        , m_camera(24.0F, {window().framebufferWidth(), window().framebufferHeight()})
        , m_random(1234) // NOLINT(bugprone-random-generator-seed): the same colours every run
    {
        m_camera.fitArea({(2.0F * kArenaHalfWidth) + 2.0F, 24.0F});
        m_camera.setPosition({0.0F, 10.0F});

        // Kinematic platforms are moved by setting their velocity; the physics does the rest.
        m_scene.addSystem(astra2d::SystemPhase::FixedUpdate, "patrol",
                          [](astra2d::Scene& scene, float) {
            scene.registry().view<Patrol, RigidBody, Transform>().each(
                [](const Patrol& patrol, RigidBody& body, const Transform& transform) {
                if (transform.position.x <= patrol.minX) {
                    body.velocity.x = patrol.speed;
                } else if (transform.position.x >= patrol.maxX) {
                    body.velocity.x = -patrol.speed;
                }
            });
        });

        // The trigger zone counts what is inside it.
        m_enter = m_physics.onTriggerEnter().connect(
            [this](const astra2d::TriggerContact& contact) { m_inZone.insert(contact.other); });
        m_exit = m_physics.onTriggerExit().connect(
            [this](const astra2d::TriggerContact& contact) { m_inZone.erase(contact.other); });
        m_hits = m_physics.onCollisionBegin().connect([this](const astra2d::CollisionEvent& event) {
            if (event.approachSpeed > 20.0F) {
                ++m_hardHits; // bullets
            }
        });
        reset();
    }

protected:
    void onFixedUpdate(float dt) override
    {
        if (m_paused && !m_stepOnce) {
            return;
        }
        m_stepOnce = false;
        pullGrabbed();
        m_scene.fixedUpdate(dt);
    }

    void onUpdate(float dt) override
    {
        const auto& in = input();
        if (in.isPressed(astra2d::Key::Escape)) {
            quit();
        }
        const glm::vec2 mouse = m_camera.screenToWorld(in.mousePosition());
        m_mouse = mouse;
        if (in.isPressed(astra2d::Key::Num1)) {
            m_spawnShape = SpawnShape::Box;
        }
        if (in.isPressed(astra2d::Key::Num2)) {
            m_spawnShape = SpawnShape::Circle;
        }
        if (in.isPressed(astra2d::Key::Num3)) {
            m_spawnShape = SpawnShape::Polygon;
        }
        if (in.isPressed(astra2d::MouseButton::Left)) {
            const Entity hit = bodyAt(mouse);
            if (!hit.isNull() && m_scene.registry().has<RigidBody>(hit) &&
                m_scene.registry().get<RigidBody>(hit).type == astra2d::BodyType::Dynamic) {
                grab(hit, mouse);
            } else {
                spawn(m_spawnShape, mouse);
            }
        }
        if (in.isReleased(astra2d::MouseButton::Left)) {
            m_grabbed = astra2d::kNullEntity;
        }
        if (in.isPressed(astra2d::MouseButton::Right)) {
            const Entity hit = bodyAt(mouse);
            if (!hit.isNull() && m_scene.registry().has<Tint>(hit)) {
                m_scene.destroyEntity(hit);
            }
        }
        if (in.isPressed(astra2d::Key::Space)) {
            shoot(mouse);
        }
        if (in.isPressed(astra2d::Key::P)) {
            pyramid(mouse, 6);
        }
        if (in.isPressed(astra2d::Key::T)) {
            tower(mouse, 10);
        }
        if (in.isPressed(astra2d::Key::B)) {
            m_broadPhase = (m_broadPhase + 1) % kBroadPhases.size();
            m_physics.world().settings().broadPhase = kBroadPhases[m_broadPhase];
        }
        if (in.isPressed(astra2d::Key::D)) {
            m_debug = !m_debug;
        }
        if (in.isPressed(astra2d::Key::F2)) {
            m_logStats = !m_logStats;
            m_frameStats.restart();
            ASTRA_INFO("Frame statistics in the log: {}", m_logStats ? "on" : "off");
        }
        m_frameTime = dt;
        if (in.isPressed(astra2d::Key::Z)) {
            auto& settings = m_physics.world().settings();
            settings.enableSleep = !settings.enableSleep;
            m_physics.world().forEachBody(
                [&](astra2d::BodyId body) { m_physics.world().setAwake(body, true); });
        }
        if (in.isPressed(astra2d::Key::Enter)) {
            m_paused = !m_paused;
        }
        if (in.isPressed(astra2d::Key::N)) {
            m_stepOnce = true;
        }
        if (in.isPressed(astra2d::Key::R)) {
            reset();
        }
        m_scene.update(dt);
        m_time += dt;
    }

    void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }

    void onRender() override
    {
        glClearColor(kBackground.r, kBackground.g, kBackground.b, kBackground.a);
        glClear(GL_COLOR_BUFFER_BIT);

        m_renderer.begin(m_camera);
        drawBodies(interpolationAlpha());
        if (m_debug) {
            drawDebug();
        }
        if (!m_grabbed.isNull() && m_scene.registry().valid(m_grabbed)) {
            const Transform world = m_scene.worldTransform(m_grabbed);
            const glm::vec2 anchor =
                world.position + astra2d::Rotation::fromAngle(world.rotation).apply(m_grabLocal);
            m_renderer.drawLine(anchor, m_mouse, {1.0F, 1.0F, 1.0F, 0.6F}, 0.05F);
        }
        m_renderer.end();

        drawHud();
        if (m_logStats && m_frameStats.addFrame(m_frameTime, m_renderer.stats())) {
            ASTRA_INFO("{}", m_frameStats.summary());
        }
        m_renderer.resetStats();
    }

private:
    // --- Level ----------------------------------------------------------------------------------

    void reset()
    {
        m_scene.clear();
        m_grabbed = astra2d::kNullEntity;
        m_inZone.clear();
        m_hardHits = 0;
        auto& registry = m_scene.registry();

        // Static colliders: entities without a RigidBody.
        addStatic({0.0F, -0.5F}, {kArenaHalfWidth + 1.0F, 0.5F});
        addStatic({-kArenaHalfWidth - 0.5F, 12.0F}, {0.5F, 12.0F});
        addStatic({kArenaHalfWidth + 0.5F, 12.0F}, {0.5F, 12.0F});
        const Entity slope = addStatic({-15.0F, 3.0F}, {4.0F, 0.25F});
        registry.get<Transform>(slope).rotation = -0.35F;

        // A floating one-way platform: jump up through it, land on it.
        const Entity platform = addStatic({-9.0F, 6.0F}, {2.5F, 0.15F});
        registry.get<Collider>(platform).oneWay = true;

        // A tile map: stairs with a one-way ledge, merged into a few boxes by the engine.
        const Entity level = m_scene.createEntity("tiles", {9.0F, 0.0F});
        astra2d::TilemapCollider tiles;
        const std::array<std::string_view, 7> rows = {
            "..........", "......----", "..........", "........##",
            "......####", "....######", "..########",
        };
        tiles.grid.width = static_cast<int>(rows[0].size());
        tiles.grid.height = static_cast<int>(rows.size());
        tiles.grid.tileSize = 1.0F;
        tiles.grid.cells.assign(rows.size() * rows[0].size(), astra2d::TileCollision::Empty);
        for (int y = 0; y < tiles.grid.height; ++y) {
            const std::string_view row = rows[static_cast<std::size_t>(tiles.grid.height - 1 - y)];
            for (int x = 0; x < tiles.grid.width; ++x) {
                const char c = row[static_cast<std::size_t>(x)];
                if (c != '.') {
                    tiles.grid.set(x, y,
                                   c == '#' ? astra2d::TileCollision::Solid
                                            : astra2d::TileCollision::OneWay);
                }
            }
        }
        registry.emplace<astra2d::TilemapCollider>(level, tiles);

        // A moving platform: kinematic, so it pushes bodies but is not pushed back.
        const Entity mover = m_scene.createEntity("mover", {-3.0F, 9.0F});
        registry.emplace<RigidBody>(
            mover, RigidBody{.type = astra2d::BodyType::Kinematic, .velocity = {2.0F, 0.0F}});
        registry.emplace<Collider>(mover, Collider{.halfExtents = {1.5F, 0.2F}});
        registry.emplace<Patrol>(mover, Patrol{.minX = -6.0F, .maxX = 4.0F});
        registry.emplace<astra2d::Interpolated>(mover);

        // A trigger zone high up: objects pass through, the HUD counts them.
        const Entity zone = m_scene.createEntity("zone", {4.0F, 15.0F});
        registry.emplace<Collider>(zone, Collider{.halfExtents = {3.0F, 2.0F}, .isTrigger = true});

        pyramid({-3.0F, 0.0F}, 7);
        m_physics.sync(); // bodies exist now, e.g. for bodyAt() before the first step
    }

    Entity addStatic(glm::vec2 position, glm::vec2 halfExtents)
    {
        const Entity e = m_scene.createEntity("static", position);
        m_scene.registry().emplace<Collider>(e, Collider{.halfExtents = halfExtents});
        return e;
    }

    glm::vec4 randomColor()
    {
        std::uniform_real_distribution<float> hue(0.0F, 1.0F);
        const float h = hue(m_random) * 6.0F;
        const float f = h - std::floor(h);
        const std::array<glm::vec3, 6> colors = {glm::vec3(1, f, 0), glm::vec3(1 - f, 1, 0),
                                                 glm::vec3(0, 1, f), glm::vec3(0, 1 - f, 1),
                                                 glm::vec3(f, 0, 1), glm::vec3(1, 0, 1 - f)};
        const glm::vec3 c = colors[static_cast<std::size_t>(h) % 6];
        return {0.35F + (0.55F * c), 1.0F};
    }

    Entity spawn(SpawnShape shape, glm::vec2 position, float angle = 0.0F)
    {
        auto& registry = m_scene.registry();
        const Entity e = m_scene.createEntity("body", position);
        registry.get<Transform>(e).rotation = angle;
        registry.emplace<RigidBody>(e);
        Collider collider{.friction = 0.6F};
        switch (shape) {
        case SpawnShape::Box:
            collider.halfExtents = {0.5F, 0.5F};
            break;
        case SpawnShape::Circle:
            collider.shape = ColliderShape::Circle;
            collider.radius = 0.45F;
            collider.restitution = 0.3F;
            break;
        case SpawnShape::Polygon: {
            collider.shape = ColliderShape::Polygon;
            std::uniform_int_distribution<int> corners(3, 7);
            const int count = corners(m_random);
            for (int i = 0; i < count; ++i) {
                const float a = static_cast<float>(i) * 2.0F * std::numbers::pi_v<float> /
                                static_cast<float>(count);
                collider.points.emplace_back(0.6F * std::cos(a), 0.6F * std::sin(a));
            }
            break;
        }
        }
        registry.emplace<Collider>(e, collider);
        registry.emplace<astra2d::Interpolated>(e);
        registry.emplace<Tint>(e, Tint{randomColor()});
        return e;
    }

    void pyramid(glm::vec2 base, int rows)
    {
        for (int row = 0; row < rows; ++row) {
            const int count = rows - row;
            for (int i = 0; i < count; ++i) {
                const float x =
                    base.x +
                    ((static_cast<float>(i) - (static_cast<float>(count - 1) * 0.5F)) * 1.05F);
                spawn(SpawnShape::Box, {x, base.y + 0.5F + static_cast<float>(row)});
            }
        }
    }

    void tower(glm::vec2 base, int height)
    {
        for (int i = 0; i < height; ++i) {
            spawn(SpawnShape::Box, {base.x, base.y + 0.5F + static_cast<float>(i)});
        }
    }

    void shoot(glm::vec2 target)
    {
        const glm::vec2 from{-kArenaHalfWidth + 1.0F, 1.5F};
        const glm::vec2 direction = glm::normalize(target - from);
        auto& registry = m_scene.registry();
        const Entity bullet = m_scene.createEntity("bullet", from);
        // 80 m/s is more than a box per step: without `bullet` it would pass through walls.
        registry.emplace<RigidBody>(
            bullet, RigidBody{.velocity = 80.0F * direction, .mass = 2.0F, .bullet = true});
        registry.emplace<Collider>(
            bullet, Collider{.shape = ColliderShape::Circle, .radius = 0.15F, .restitution = 0.2F});
        registry.emplace<astra2d::Interpolated>(bullet);
        registry.emplace<Tint>(bullet, Tint{{1.0F, 1.0F, 0.6F, 1.0F}});
    }

    // --- Mouse spring -------------------------------------------------------------------------

    Entity bodyAt(glm::vec2 point) const
    {
        const auto& world = m_physics.world();
        for (const astra2d::ShapeId shape : world.queryPoint(point)) {
            if (!world.shapeDef(shape).isTrigger) {
                return m_physics.entity(world.body(shape));
            }
        }
        return astra2d::kNullEntity;
    }

    void grab(Entity entity, glm::vec2 point)
    {
        m_grabbed = entity;
        const Transform world = m_scene.worldTransform(entity);
        m_grabLocal =
            astra2d::Rotation::fromAngle(world.rotation).applyInverse(point - world.position);
    }

    // A damped spring from the grabbed point to the mouse, as a force at that point (so the body
    // also turns), scaled by the mass so heavy and light bodies follow alike.
    void pullGrabbed()
    {
        if (m_grabbed.isNull() || !m_scene.registry().valid(m_grabbed)) {
            m_grabbed = astra2d::kNullEntity;
            return;
        }
        auto& world = m_physics.world();
        const astra2d::BodyId body = m_physics.body(m_grabbed);
        if (!world.valid(body)) {
            return;
        }
        constexpr float kStiffness = 80.0F;
        constexpr float kDamping = 12.0F;
        const glm::vec2 anchor = world.pose(body).apply(m_grabLocal);
        const glm::vec2 velocity =
            world.velocity(body) +
            astra2d::cross(world.angularVelocity(body), anchor - world.worldCenter(body));
        const glm::vec2 force =
            world.mass(body) * ((kStiffness * (m_mouse - anchor)) - (kDamping * velocity));
        world.applyForce(body, force - (world.mass(body) * world.settings().gravity), anchor);
    }

    // --- Drawing --------------------------------------------------------------------------------

    void drawBodies(float alpha)
    {
        auto& registry = m_scene.registry();
        const auto& world = m_physics.world();
        registry.view<Collider, Transform>().each(
            [&](Entity e, const Collider& collider, const Transform&) {
            const Transform t = m_scene.worldTransform(e, alpha);
            glm::vec4 color = kStatic;
            if (const auto* tint = registry.tryGet<Tint>(e)) {
                color = tint->color;
                const astra2d::BodyId body = m_physics.body(e);
                if (world.valid(body) && !world.isAwake(body)) {
                    color = glm::vec4(glm::vec3(color) * 0.45F, 1.0F); // asleep: dimmed
                }
            } else if (registry.has<RigidBody>(e)) {
                color = kKinematic;
            }
            if (collider.isTrigger) {
                color = kTrigger;
            } else if (collider.oneWay) {
                color = kOneWay;
            }
            drawCollider(collider, t, color);
        });
        registry.view<astra2d::TilemapCollider, Transform>().each(
            [&](Entity e, const astra2d::TilemapCollider& tiles, const Transform&) {
            const glm::vec2 origin = m_scene.worldTransform(e).position + tiles.grid.origin;
            for (const astra2d::TileRect& r : astra2d::mergeTiles(tiles.grid)) {
                const glm::vec2 size =
                    glm::vec2(static_cast<float>(r.width), static_cast<float>(r.height)) *
                    tiles.grid.tileSize;
                const glm::vec2 center =
                    origin +
                    (glm::vec2(static_cast<float>(r.x), static_cast<float>(r.y)) *
                     tiles.grid.tileSize) +
                    (0.5F * size);
                const bool oneWay = r.kind == astra2d::TileCollision::OneWay;
                m_renderer.drawQuad(center, oneWay ? glm::vec2(size.x, 0.15F * size.y) : size,
                                    oneWay ? kOneWay : kStatic);
                if (oneWay) {
                    continue;
                }
                // The merged rectangles, to show what the engine made of the tiles.
                m_renderer.drawRect(center, size, {0.0F, 0.0F, 0.0F, 0.35F}, 0.04F);
            }
        });
        m_renderer.drawText(m_font, "trigger zone", {1.4F, 16.6F}, 0.45F, {0.4F, 1.0F, 0.6F, 0.9F});
    }

    void drawCollider(const Collider& collider, const Transform& t, glm::vec4 color)
    {
        const astra2d::Rotation rotation = astra2d::Rotation::fromAngle(t.rotation);
        const glm::vec2 center = t.position + rotation.apply(collider.offset);
        const glm::vec4 edge{glm::vec3(color) * 0.6F, 1.0F};
        switch (collider.shape) {
        case ColliderShape::Box:
            m_renderer.drawQuad(center, 2.0F * collider.halfExtents, color, t.rotation);
            break;
        case ColliderShape::Circle:
            m_renderer.drawCircle(center, collider.radius - 0.06F, color, 0.12F, 24);
            m_renderer.drawCircle(center, collider.radius * 0.45F, color, collider.radius * 0.45F,
                                  12);
            // A spoke shows the rotation (circles roll).
            m_renderer.drawLine(center, center + rotation.apply({collider.radius, 0.0F}), edge,
                                0.08F);
            break;
        case ColliderShape::Polygon: {
            const auto& points = collider.points;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const glm::vec2 a = t.position + rotation.apply(collider.offset + points[i]);
                const glm::vec2 b =
                    t.position + rotation.apply(collider.offset + points[(i + 1) % points.size()]);
                m_renderer.drawLine(a, b, color, 0.12F);
                m_renderer.drawLine(center, a, edge, 0.05F);
            }
            break;
        }
        }
    }

    // The engine's physics debug view, on top of the bodies: outlines coloured by body state,
    // bounding boxes, contact points with normals, velocity arrows.
    void drawDebug()
    {
        astra2d::drawPhysicsDebug(m_renderer, m_physics.world(), {.boundingBoxes = true});
    }

    void drawHud()
    {
        const int width = window().framebufferWidth();
        const int height = window().framebufferHeight();
        m_renderer.begin(astra2d::OrthographicCamera::screenSpace(width, height));
        const auto& stats = m_physics.world().stats();
        const auto& settings = m_physics.world().settings();
        const std::array<std::string_view, 3> shapeNames = {"box", "circle", "polygon"};
        const std::array<std::string, 6> lines = {
            std::format("bodies {}  awake {}  contacts {}", stats.bodies, stats.awakeBodies,
                        stats.touchingContacts),
            std::format("broad phase: {} (B)  {} box tests, {} pairs",
                        broadPhaseName(settings.broadPhase), stats.pairTests, stats.candidatePairs),
            std::format("step {:.2f} ms (broad {:.2f}  narrow {:.2f}  solve {:.2f})", stats.stepMs,
                        stats.broadPhaseMs, stats.narrowPhaseMs, stats.solverMs),
            std::format("spawn: {} (1/2/3)  sleep {} (Z)  debug {} (D){}",
                        shapeNames[static_cast<std::size_t>(m_spawnShape)],
                        settings.enableSleep ? "on" : "off", m_debug ? "on" : "off",
                        m_paused ? "  PAUSED (N steps)" : ""),
            std::format("in trigger zone {}  bullet hits {}", m_inZone.size(), m_hardHits),
            "click spawn/drag  right click delete  Space shoot  P pyramid  T tower  R reset",
        };
        std::string text;
        for (const std::string& line : lines) {
            text += (text.empty() ? "" : "\n") + line;
        }
        // Inside the walls, which start 16 pixels in at the default zoom.
        examples::drawTextPanel(m_renderer, m_font, text,
                                {80.0F, static_cast<float>(height) - 40.0F}, 16.0F, kText, 1.5F);
        m_renderer.end();
    }

    // --- State ----------------------------------------------------------------------------------

    astra2d::AssetCache m_assets;
    astra2d::Font m_font;
    astra2d::OrthographicCamera m_camera;
    astra2d::Renderer2D m_renderer;
    astra2d::Scene m_scene;
    astra2d::PhysicsSystem m_physics{m_scene}; // after the scene: destroyed before it
    astra2d::Connection m_enter;
    astra2d::Connection m_exit;
    astra2d::Connection m_hits;
    std::mt19937 m_random;

    SpawnShape m_spawnShape = SpawnShape::Box;
    std::size_t m_broadPhase = 0;
    bool m_debug = false;
    astra2d::FrameStats m_frameStats;
    bool m_logStats = false;
    float m_frameTime = 0.0F;
    bool m_paused = false;
    bool m_stepOnce = false;
    glm::vec2 m_mouse{0.0F};
    Entity m_grabbed;
    glm::vec2 m_grabLocal{0.0F};
    std::unordered_set<Entity> m_inZone; // entities in the trigger zone
    int m_hardHits = 0;
    float m_time = 0.0F;
};

} // namespace

int main()
{
    try {
        PhysicsPlayground app;
        app.run();
    } catch (const std::exception& error) {
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
