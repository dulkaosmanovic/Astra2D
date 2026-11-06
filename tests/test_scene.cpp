#include <astra2d/core/assert.hpp>
#include <astra2d/scene/components.hpp>
#include <astra2d/scene/scene.hpp>

#include <gtest/gtest.h>

#include <numbers>
#include <string>
#include <vector>

using astra2d::Entity;
using astra2d::Name;
using astra2d::Relationship;
using astra2d::Scene;
using astra2d::SystemPhase;
using astra2d::Transform;

namespace {

constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0F;

void expectNear(glm::vec2 actual, glm::vec2 expected)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-4F);
    EXPECT_NEAR(actual.y, expected.y, 1e-4F);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Entities

TEST(SceneTests, CreateEntityAddsTransformAndName)
{
    Scene scene;
    const Entity named = scene.createEntity("player", {3.0F, 4.0F});
    const Entity anonymous = scene.createEntity();
    expectNear(scene.registry().get<Transform>(named).position, {3.0F, 4.0F});
    EXPECT_EQ(scene.registry().get<Name>(named).value, "player");
    EXPECT_TRUE(scene.registry().has<Transform>(anonymous));
    EXPECT_FALSE(scene.registry().has<Name>(anonymous));
    EXPECT_EQ(scene.find("player"), named);
    EXPECT_TRUE(scene.find("nobody").isNull());
}

TEST(SceneTests, DestroyLaterWaitsForTheEndOfTheStep)
{
    Scene scene;
    const Entity entity = scene.createEntity();
    bool aliveDuringSystem = false;
    scene.addSystem(SystemPhase::Update, "killer", [&](Scene& s, float) {
        s.destroyLater(entity);
        s.destroyLater(entity); // queued twice: destroyed once, no assertion
        aliveDuringSystem = s.registry().valid(entity);
    });
    scene.update(0.016F);
    EXPECT_TRUE(aliveDuringSystem);
    EXPECT_FALSE(scene.registry().valid(entity));
}

TEST(SceneTests, DestroyLaterIsSafeInsideAView)
{
    Scene scene;
    for (int i = 0; i < 20; ++i) {
        scene.createEntity({}, {static_cast<float>(i), 0.0F});
    }
    int visited = 0;
    scene.registry().view<Transform>().each([&](Entity e, Transform& t) {
        ++visited;
        if (static_cast<int>(t.position.x) < 10) {
            scene.destroyLater(e);
        }
    });
    scene.flushDestroyed();
    EXPECT_EQ(visited, 20);
    EXPECT_EQ(scene.registry().alive(), 10U);
}

// ---------------------------------------------------------------------------------------------
// Systems

TEST(SceneTests, SystemsRunInPhaseThenInsertionOrder)
{
    Scene scene;
    std::vector<std::string> log;
    scene.addSystem(SystemPhase::LateUpdate, "late",
                    [&](Scene&, float) { log.emplace_back("late"); });
    scene.addSystem(SystemPhase::Update, "b", [&](Scene&, float) { log.emplace_back("b"); });
    scene.addSystem(SystemPhase::Update, "a", [&](Scene&, float) { log.emplace_back("a"); });
    scene.addSystem(SystemPhase::FixedUpdate, "fixed", [&](Scene&, float dt) {
        log.push_back("fixed " + std::to_string(static_cast<int>(dt * 1000.0F)));
    });

    scene.fixedUpdate(0.02F);
    scene.update(0.016F);
    EXPECT_EQ(log, (std::vector<std::string>{"fixed 20", "b", "a", "late"}));
}

TEST(SceneTests, BuiltInAnimationSystemCanBeRemoved)
{
    Scene scene;
    EXPECT_EQ(scene.systemNames(SystemPhase::LateUpdate), std::vector<std::string>{"animation"});
    EXPECT_TRUE(scene.removeSystem("animation"));
    EXPECT_FALSE(scene.removeSystem("animation"));
    EXPECT_TRUE(scene.systemNames(SystemPhase::LateUpdate).empty());
}

TEST(SceneTests, SystemAddedBySystemRunsFromTheNextStep)
{
    Scene scene;
    int lateCalls = 0;
    scene.addSystem(SystemPhase::Update, "adder", [&](Scene& s, float) {
        if (s.systemNames(SystemPhase::Update).size() == 1) {
            s.addSystem(SystemPhase::Update, "late", [&](Scene&, float) { ++lateCalls; });
        }
    });
    scene.update(0.0F);
    EXPECT_EQ(lateCalls, 0);
    scene.update(0.0F);
    EXPECT_EQ(lateCalls, 1);
}

#if ASTRA_ASSERTS_ENABLED
TEST(SceneDeathTest, DuplicateSystemNameAsserts)
{
    Scene scene;
    EXPECT_DEATH(scene.addSystem(SystemPhase::Update, "animation", [](Scene&, float) {}),
                 "exists already");
}
#endif

// ---------------------------------------------------------------------------------------------
// Transforms and hierarchy

TEST(TransformTests, ComposeAppliesParentScaleRotationAndPosition)
{
    const Transform parent{.position = {10.0F, 0.0F}, .rotation = kHalfPi, .scale = {2.0F, 2.0F}};
    const Transform local{.position = {1.0F, 0.0F}, .rotation = 0.1F, .scale = {3.0F, 1.0F}};
    const Transform world = astra2d::compose(parent, local);
    expectNear(world.position, {10.0F, 2.0F}); // (1,0) scaled to (2,0), turned 90° to (0,2)
    EXPECT_NEAR(world.rotation, kHalfPi + 0.1F, 1e-5F);
    expectNear(world.scale, {6.0F, 2.0F});
}

TEST(TransformTests, RelativeToInvertsCompose)
{
    const Transform parent{.position = {-3.0F, 5.0F}, .rotation = 0.7F, .scale = {2.0F, 0.5F}};
    const Transform local{.position = {4.0F, -1.0F}, .rotation = -0.2F, .scale = {1.5F, 1.0F}};
    const Transform back = astra2d::relativeTo(parent, astra2d::compose(parent, local));
    expectNear(back.position, local.position);
    EXPECT_NEAR(back.rotation, local.rotation, 1e-5F);
    expectNear(back.scale, local.scale);
}

TEST(HierarchyTests, ChildFollowsParent)
{
    Scene scene;
    const Entity sun = scene.createEntity("sun", {100.0F, 0.0F});
    const Entity planet = scene.createEntity("planet", {10.0F, 0.0F});
    scene.setParent(planet, sun);

    expectNear(scene.worldTransform(planet).position, {110.0F, 0.0F});
    scene.registry().get<Transform>(sun).rotation = kHalfPi; // turning the sun moves the planet
    expectNear(scene.worldTransform(planet).position, {100.0F, 10.0F});
    EXPECT_EQ(scene.parent(planet), sun);
    EXPECT_EQ(scene.children(sun), std::vector<Entity>{planet});
}

TEST(HierarchyTests, ChildrenKeepAttachOrderAndCanBeDetached)
{
    Scene scene;
    const Entity parent = scene.createEntity();
    const Entity a = scene.createEntity();
    const Entity b = scene.createEntity();
    const Entity c = scene.createEntity();
    scene.setParent(a, parent);
    scene.setParent(b, parent);
    scene.setParent(c, parent);
    EXPECT_EQ(scene.children(parent), (std::vector<Entity>{a, b, c}));

    scene.setParent(b, astra2d::kNullEntity); // middle
    EXPECT_EQ(scene.children(parent), (std::vector<Entity>{a, c}));
    scene.setParent(a, astra2d::kNullEntity); // first
    EXPECT_EQ(scene.children(parent), std::vector<Entity>{c});
    EXPECT_EQ(scene.registry().get<Relationship>(parent).children, 1U);
    EXPECT_TRUE(scene.parent(a).isNull());
}

TEST(HierarchyTests, ReparentingMovesBetweenParents)
{
    Scene scene;
    const Entity first = scene.createEntity();
    const Entity second = scene.createEntity();
    const Entity child = scene.createEntity();
    scene.setParent(child, first);
    scene.setParent(child, second);
    EXPECT_TRUE(scene.children(first).empty());
    EXPECT_EQ(scene.children(second), std::vector<Entity>{child});
}

TEST(HierarchyTests, KeepWorldTransformLeavesTheChildInPlace)
{
    Scene scene;
    const Entity parent = scene.createEntity({}, {50.0F, 50.0F});
    scene.registry().get<Transform>(parent).rotation = 0.5F;
    scene.registry().get<Transform>(parent).scale = {2.0F, 2.0F};
    const Entity child = scene.createEntity({}, {7.0F, -3.0F});

    scene.setParent(child, parent, /*keepWorldTransform=*/true);
    expectNear(scene.worldTransform(child).position, {7.0F, -3.0F});
    EXPECT_NEAR(scene.worldTransform(child).rotation, 0.0F, 1e-5F);

    scene.setParent(child, astra2d::kNullEntity, true);
    expectNear(scene.registry().get<Transform>(child).position, {7.0F, -3.0F});
}

TEST(HierarchyTests, DestroyEntityDestroysDescendants)
{
    Scene scene;
    const Entity root = scene.createEntity();
    const Entity middle = scene.createEntity();
    const Entity leaf = scene.createEntity();
    const Entity other = scene.createEntity();
    scene.setParent(middle, root);
    scene.setParent(leaf, middle);

    scene.destroyEntity(root);
    EXPECT_FALSE(scene.registry().valid(root));
    EXPECT_FALSE(scene.registry().valid(middle));
    EXPECT_FALSE(scene.registry().valid(leaf));
    EXPECT_TRUE(scene.registry().valid(other));
}

TEST(HierarchyTests, DestroyingAChildUnlinksIt)
{
    Scene scene;
    const Entity parent = scene.createEntity();
    const Entity a = scene.createEntity();
    const Entity b = scene.createEntity();
    scene.setParent(a, parent);
    scene.setParent(b, parent);
    scene.destroyEntity(a);
    EXPECT_EQ(scene.children(parent), std::vector<Entity>{b});
}

TEST(HierarchyTests, RawRegistryDestroyOrphansChildren)
{
    // Registry::destroy() bypasses the Scene; the onDestroy signal still keeps links consistent.
    Scene scene;
    const Entity parent = scene.createEntity({}, {100.0F, 0.0F});
    const Entity child = scene.createEntity({}, {1.0F, 0.0F});
    scene.setParent(child, parent);
    scene.registry().destroy(parent);
    EXPECT_TRUE(scene.registry().valid(child));
    EXPECT_TRUE(scene.parent(child).isNull());
    expectNear(scene.worldTransform(child).position, {1.0F, 0.0F});
}

#if ASTRA_ASSERTS_ENABLED
TEST(HierarchyDeathTest, CyclesAreRejected)
{
    Scene scene;
    const Entity a = scene.createEntity();
    const Entity b = scene.createEntity();
    scene.setParent(b, a);
    EXPECT_DEATH(scene.setParent(a, b), "cycle");
    EXPECT_DEATH(scene.setParent(a, a), "cycle");
}
#endif
