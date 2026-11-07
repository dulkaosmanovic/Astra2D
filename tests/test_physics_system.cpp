// PhysicsSystem: RigidBody / Collider / TilemapCollider <-> PhysicsWorld, signals, queries.

#include <astra2d/scene/components.hpp>
#include <astra2d/scene/physics_system.hpp>
#include <astra2d/scene/scene.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using astra2d::BodyType;
using astra2d::Collider;
using astra2d::ColliderShape;
using astra2d::Entity;
using astra2d::PhysicsSystem;
using astra2d::RigidBody;
using astra2d::Scene;
using astra2d::SystemPhase;
using astra2d::Transform;

namespace {

constexpr float kDt = 1.0F / 60.0F;

void run(Scene& scene, float seconds)
{
    const int steps = static_cast<int>(std::lround(seconds / kDt));
    for (int i = 0; i < steps; ++i) {
        scene.fixedUpdate(kDt);
    }
}

// Ground with its top at y = 0 (a Collider without RigidBody: static).
Entity addGround(Scene& scene)
{
    const Entity ground = scene.createEntity("ground", {0.0F, -0.5F});
    scene.registry().emplace<Collider>(ground, Collider{.halfExtents = {50.0F, 0.5F}});
    return ground;
}

Entity addBox(Scene& scene, glm::vec2 position, RigidBody body = {})
{
    const Entity box = scene.createEntity("box", position);
    scene.registry().emplace<RigidBody>(box, body);
    scene.registry().emplace<Collider>(box);
    return box;
}

} // namespace

TEST(PhysicsSystemTests, AddsTheSystemAndRemovesItAgain)
{
    Scene scene;
    {
        const PhysicsSystem physics(scene);
        EXPECT_EQ(scene.systemNames(SystemPhase::FixedUpdate), std::vector<std::string>{"physics"});
    }
    EXPECT_TRUE(scene.systemNames(SystemPhase::FixedUpdate).empty());
}

TEST(PhysicsSystemTests, DynamicEntityFallsOntoAStaticCollider)
{
    Scene scene;
    PhysicsSystem physics(scene);
    const Entity ground = addGround(scene);
    const Entity box = addBox(scene, {0.0F, 3.0F});
    scene.fixedUpdate(kDt);
    EXPECT_EQ(physics.world().type(physics.body(ground)), BodyType::Static);
    EXPECT_EQ(physics.entity(physics.body(box)), box);
    EXPECT_LT(scene.registry().get<RigidBody>(box).velocity.y, 0.0F); // written back
    run(scene, 2.0F);
    EXPECT_NEAR(scene.registry().get<Transform>(box).position.y, 0.5F, 0.02F);
    EXPECT_TRUE(physics.isGrounded(box));
    EXPECT_EQ(scene.registry().get<Transform>(ground).position, glm::vec2(0.0F, -0.5F));
}

TEST(PhysicsSystemTests, GameChangesToComponentsAreApplied)
{
    Scene scene;
    PhysicsSystem physics(scene, {.gravity = {0.0F, 0.0F}});
    const Entity box = addBox(scene, {0.0F, 0.0F});
    run(scene, 0.5F);

    // Velocity set by the game.
    scene.registry().get<RigidBody>(box).velocity = {2.0F, 0.0F};
    run(scene, 1.0F);
    EXPECT_NEAR(scene.registry().get<Transform>(box).position.x, 2.0F, 1e-3F);

    // Moving the Transform teleports the body.
    scene.registry().get<Transform>(box).position = {-10.0F, 4.0F};
    scene.fixedUpdate(kDt);
    EXPECT_NEAR(physics.world().position(physics.body(box)).x, -10.0F + (2.0F * kDt), 1e-4F);
    EXPECT_NEAR(scene.registry().get<Transform>(box).position.y, 4.0F, 1e-5F);

    // Changing the type: a kinematic body ignores gravity, a static one stops.
    physics.world().settings().gravity = {0.0F, -10.0F};
    scene.registry().get<RigidBody>(box).type = BodyType::Kinematic;
    run(scene, 0.5F);
    EXPECT_NEAR(scene.registry().get<RigidBody>(box).velocity.y, 0.0F, 1e-6F);
    scene.registry().get<RigidBody>(box).type = BodyType::Static;
    const glm::vec2 stopped = scene.registry().get<Transform>(box).position;
    run(scene, 0.5F);
    EXPECT_EQ(scene.registry().get<Transform>(box).position, stopped);

    // A bigger collider rebuilds the shape.
    scene.registry().get<Collider>(box) = Collider{.shape = ColliderShape::Circle, .radius = 2.0F};
    scene.fixedUpdate(kDt);
    const auto shapes = physics.world().shapes(physics.body(box));
    ASSERT_EQ(shapes.size(), 1U);
    EXPECT_EQ(physics.world().shapeDef(shapes[0]).type, astra2d::ShapeType::Circle);
    EXPECT_FLOAT_EQ(physics.world().shapeDef(shapes[0]).circle.radius, 2.0F);
}

TEST(PhysicsSystemTests, RemovingComponentsAndDestroyingEntities)
{
    Scene scene;
    PhysicsSystem physics(scene);
    addGround(scene);
    const Entity box = addBox(scene, {0.0F, 3.0F});
    scene.fixedUpdate(kDt);
    const astra2d::BodyId before = physics.body(box);

    // Without RigidBody, the collider stays as a static body.
    scene.registry().remove<RigidBody>(box);
    EXPECT_FALSE(physics.world().valid(before));
    scene.fixedUpdate(kDt);
    ASSERT_TRUE(physics.world().valid(physics.body(box)));
    EXPECT_EQ(physics.world().type(physics.body(box)), BodyType::Static);

    // A body with neither a collider nor a rigid body has no body.
    scene.registry().remove<Collider>(box);
    scene.fixedUpdate(kDt);
    EXPECT_TRUE(physics.body(box).isNull());

    const Entity other = addBox(scene, {5.0F, 3.0F});
    scene.fixedUpdate(kDt);
    const astra2d::BodyId otherBody = physics.body(other);
    scene.destroyEntity(other);
    EXPECT_FALSE(physics.world().valid(otherBody));
    EXPECT_EQ(physics.world().bodyCount(), 1U); // the ground

    scene.clear();
    EXPECT_EQ(physics.world().bodyCount(), 0U);
}

TEST(PhysicsSystemTests, CollisionSignalsNameTheEntities)
{
    Scene scene;
    PhysicsSystem physics(scene, {.enableSleep = false});
    const Entity ground = addGround(scene);
    const Entity box = addBox(scene, {0.0F, 2.0F});
    std::vector<astra2d::CollisionEvent> begins;
    std::vector<astra2d::CollisionEvent> ends;
    const auto beginConnection = physics.onCollisionBegin().connect(
        [&](const astra2d::CollisionEvent& e) { begins.push_back(e); });
    const auto endConnection = physics.onCollisionEnd().connect(
        [&](const astra2d::CollisionEvent& e) { ends.push_back(e); });
    run(scene, 1.0F);
    ASSERT_EQ(begins.size(), 1U);
    EXPECT_EQ(begins[0].other(box), ground);
    EXPECT_EQ(begins[0].other(ground), box);
    EXPECT_GT(begins[0].approachSpeed, 3.0F);

    // Destroying the box in the game: the end event comes with the next step, naming it.
    scene.destroyEntity(box);
    scene.fixedUpdate(kDt);
    ASSERT_EQ(ends.size(), 1U);
    EXPECT_EQ(ends[0].other(ground), box);
    EXPECT_FALSE(scene.registry().valid(box));
}

TEST(PhysicsSystemTests, TriggerHandlersMayDestroyEntities)
{
    Scene scene;
    PhysicsSystem physics(scene);
    addGround(scene);
    // Coins as static triggers in the path of a falling player.
    std::vector<Entity> coins;
    for (int i = 0; i < 3; ++i) {
        const Entity coin = scene.createEntity("coin", {0.0F, 1.0F + static_cast<float>(i)});
        scene.registry().emplace<Collider>(
            coin, Collider{.shape = ColliderShape::Circle, .radius = 0.2F, .isTrigger = true});
        coins.push_back(coin);
    }
    const Entity player = addBox(scene, {0.0F, 5.0F}, {.fixedRotation = true});
    int collected = 0;
    int exits = 0;
    const auto enter =
        physics.onTriggerEnter().connect([&](const astra2d::TriggerContact& contact) {
        EXPECT_EQ(contact.other, player);
        ++collected;
        scene.destroyEntity(contact.trigger); // right away, inside the handler
    });
    const auto exit =
        physics.onTriggerExit().connect([&](const astra2d::TriggerContact&) { ++exits; });
    run(scene, 2.0F);
    EXPECT_EQ(collected, 3);
    EXPECT_EQ(exits, 3); // the destroyed coins end their overlaps too
    for (const Entity coin : coins) {
        EXPECT_FALSE(scene.registry().valid(coin));
    }
    EXPECT_NEAR(scene.registry().get<Transform>(player).position.y, 0.5F,
                0.02F); // fell through them
}

TEST(PhysicsSystemTests, SystemsBeforePhysicsSeeTheResultsOfTheLastStep)
{
    Scene scene;
    // A controller added before the physics: sets the velocity, the physics applies it.
    scene.addSystem(SystemPhase::FixedUpdate, "controller", [](Scene& s, float) {
        s.registry().view<RigidBody>().each([](RigidBody& body) { body.velocity.x = 3.0F; });
    });
    PhysicsSystem physics(scene, {.gravity = {0.0F, 0.0F}});
    const Entity box = addBox(scene, {0.0F, 0.0F});
    run(scene, 1.0F);
    EXPECT_NEAR(scene.registry().get<Transform>(box).position.x, 3.0F, 1e-3F);
    EXPECT_EQ(scene.systemNames(SystemPhase::FixedUpdate),
              (std::vector<std::string>{"controller", "physics"}));
}

TEST(PhysicsSystemTests, ChildEntitiesUseWorldPositions)
{
    Scene scene;
    PhysicsSystem physics(scene, {.gravity = {0.0F, -10.0F}});
    const Entity parent = scene.createEntity("parent", {10.0F, 0.0F});
    const Entity child = addBox(scene, {0.0F, 5.0F});
    scene.setParent(child, parent);
    scene.fixedUpdate(kDt);
    EXPECT_NEAR(physics.world().position(physics.body(child)).x, 10.0F, 1e-5F);
    run(scene, 0.5F);
    const Transform world = scene.worldTransform(child);
    EXPECT_NEAR(world.position.x, 10.0F, 1e-4F);
    EXPECT_LT(world.position.y, 4.0F);
    EXPECT_NEAR(scene.registry().get<Transform>(child).position.x, 0.0F, 1e-4F); // still local
}

TEST(PhysicsSystemTests, BodiesAtRestInAHierarchyFallAsleep)
{
    // Rounding when converting between local and world positions must not look like a teleport
    // every step (which would keep the body awake).
    Scene scene;
    PhysicsSystem physics(scene);
    addGround(scene);
    const Entity parent = scene.createEntity("parent", {0.3F, 0.1F});
    scene.registry().get<Transform>(parent).rotation = 0.0F;
    const Entity child = addBox(scene, {0.7F, 0.6F});
    scene.setParent(child, parent);
    run(scene, 3.0F);
    EXPECT_FALSE(physics.world().isAwake(physics.body(child)));
}

TEST(PhysicsSystemTests, TilemapColliderAndRayCast)
{
    Scene scene;
    PhysicsSystem physics(scene);
    const Entity level = scene.createEntity("level", {-4.0F, -1.0F});
    astra2d::TilemapCollider tiles;
    tiles.grid.width = 8;
    tiles.grid.height = 1;
    tiles.grid.cells.assign(8, astra2d::TileCollision::Solid);
    scene.registry().emplace<astra2d::TilemapCollider>(level, tiles);
    const Entity box = addBox(scene, {0.0F, 2.0F});
    run(scene, 2.0F);
    EXPECT_NEAR(scene.registry().get<Transform>(box).position.y, 0.5F, 0.02F);

    const auto hit = physics.rayCast({-2.0F, 5.0F}, {0.0F, -10.0F});
    ASSERT_TRUE(hit.has_value());
    const astra2d::EntityRayHit h = hit.value_or(astra2d::EntityRayHit{});
    EXPECT_EQ(h.entity, level);
    EXPECT_NEAR(h.point.y, 0.0F, 1e-4F);
}

TEST(PhysicsSystemTests, BadPolygonIsIgnored)
{
    Scene scene;
    PhysicsSystem physics(scene);
    const Entity e = scene.createEntity();
    scene.registry().emplace<Collider>(
        e, Collider{.shape = ColliderShape::Polygon, .points = {{0, 0}, {1, 1}}});
    scene.fixedUpdate(kDt);
    ASSERT_TRUE(physics.world().valid(physics.body(e)));
    EXPECT_TRUE(physics.world().shapes(physics.body(e)).empty());
}

TEST(PhysicsSystemTests, BadMassFallsBackToOne)
{
    Scene scene;
    PhysicsSystem physics(scene);
    addGround(scene);
    const Entity box = addBox(scene, {0.0F, 2.0F});
    scene.registry().get<RigidBody>(box).mass = 0.0F;
    run(scene, 1.0F);
    const Transform& t = scene.registry().get<Transform>(box);
    EXPECT_TRUE(std::isfinite(t.position.x) && std::isfinite(t.position.y));
    EXPECT_FLOAT_EQ(physics.world().mass(physics.body(box)), 1.0F);
}

TEST(PhysicsSystemTests, BadColliderSizesAreIgnored)
{
    Scene scene;
    PhysicsSystem physics(scene);
    const Entity box = scene.createEntity();
    scene.registry().emplace<Collider>(box, Collider{.halfExtents = {0.0F, 1.0F}});
    const Entity circle = addBox(scene, {0.0F, 0.0F});
    scene.registry().get<Collider>(circle) =
        Collider{.shape = ColliderShape::Circle, .radius = -1.0F};
    const Entity tiles = scene.createEntity();
    astra2d::TilemapCollider tilemap;
    tilemap.grid.width = 2;
    tilemap.grid.height = 1;
    tilemap.grid.tileSize = 0.0F;
    tilemap.grid.cells.assign(2, astra2d::TileCollision::Solid);
    scene.registry().emplace<astra2d::TilemapCollider>(tiles, tilemap);
    run(scene, 0.1F);
    for (const Entity e : {box, circle, tiles}) {
        ASSERT_TRUE(physics.world().valid(physics.body(e)));
        EXPECT_TRUE(physics.world().shapes(physics.body(e)).empty());
    }
    // A dynamic body without shapes still falls, with a finite position.
    EXPECT_TRUE(std::isfinite(scene.registry().get<Transform>(circle).position.y));
    EXPECT_LT(scene.registry().get<Transform>(circle).position.y, 0.0F);
}

TEST(PhysicsSystemTests, ChildBodyOfAMovingParentBodyIsNotDragged)
{
    // Views run from the last added entity, so the child's RigidBody comes first and would be
    // written back before the parent moves; it must still be converted against the parent's new
    // pose.
    Scene scene;
    PhysicsSystem physics(scene, {.gravity = {0.0F, 0.0F}});
    const Entity parent = addBox(scene, {0.0F, 0.0F}, {.velocity = {5.0F, 0.0F}});
    const Entity child = addBox(scene, {0.0F, 5.0F});
    scene.setParent(child, parent);
    run(scene, 1.0F);
    EXPECT_NEAR(scene.worldTransform(parent).position.x, 5.0F, 0.01F);
    const Transform world = scene.worldTransform(child);
    EXPECT_NEAR(world.position.x, 0.0F, 1e-3F);
    EXPECT_NEAR(world.position.y, 5.0F, 1e-3F);
    EXPECT_NEAR(physics.world().position(physics.body(child)).x, 0.0F, 1e-3F);
    EXPECT_NEAR(scene.registry().get<Transform>(child).position.x, -5.0F, 0.01F); // local
}
