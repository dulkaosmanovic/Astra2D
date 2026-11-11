// Moving an entity with the mouse in the overlay's move mode: roots, children of moved, rotated
// and scaled parents, and bodies that must stop.

#include "debug/entity_moving.hpp"

#include <astra2d/scene/components.hpp>
#include <astra2d/scene/scene.hpp>

#include <gtest/gtest.h>

#include <numbers>

using astra2d::Entity;
using astra2d::RigidBody;
using astra2d::Scene;
using astra2d::Transform;
using astra2d::detail::moveEntityTo;

namespace {

void expectNear(glm::vec2 actual, glm::vec2 expected)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-4F);
    EXPECT_NEAR(actual.y, expected.y, 1e-4F);
}

} // namespace

TEST(EntityMovingTests, RootGetsTheWorldPosition)
{
    Scene scene;
    const Entity entity = scene.createEntity("box", {1.0F, 2.0F});
    scene.registry().get<Transform>(entity).rotation = 0.5F;
    moveEntityTo(scene, entity, {7.0F, -3.0F});
    const Transform& transform = scene.registry().get<Transform>(entity);
    expectNear(transform.position, {7.0F, -3.0F});
    EXPECT_FLOAT_EQ(transform.rotation, 0.5F); // only the position changes
}

TEST(EntityMovingTests, ChildOfARotatedScaledParentEndsUpAtTheWorldPoint)
{
    Scene scene;
    const Entity parent = scene.createEntity("parent", {10.0F, 0.0F});
    auto& parentTransform = scene.registry().get<Transform>(parent);
    parentTransform.rotation = std::numbers::pi_v<float> / 2.0F;
    parentTransform.scale = {2.0F, 2.0F};
    const Entity child = scene.createEntity("child");
    scene.setParent(child, parent);

    moveEntityTo(scene, child, {4.0F, 6.0F});
    expectNear(scene.worldTransform(child).position, {4.0F, 6.0F});
    // In the parent's space: 6 to the left and 6 up, turned back a quarter and halved.
    expectNear(scene.registry().get<Transform>(child).position, {3.0F, 3.0F});
}

TEST(EntityMovingTests, BodyStopsWhileHeld)
{
    Scene scene;
    const Entity entity = scene.createEntity("crate");
    scene.registry().emplace<RigidBody>(
        entity, RigidBody{.velocity = {3.0F, -9.0F}, .angularVelocity = 2.0F});
    moveEntityTo(scene, entity, {1.0F, 1.0F});
    const RigidBody& body = scene.registry().get<RigidBody>(entity);
    expectNear(body.velocity, {0.0F, 0.0F});
    EXPECT_FLOAT_EQ(body.angularVelocity, 0.0F);
}

TEST(EntityMovingTests, InterpolatedEntityDoesNotSlide)
{
    Scene scene;
    const Entity entity = scene.createEntity("player");
    scene.registry().emplace<astra2d::Interpolated>(entity);
    scene.fixedUpdate(1.0F / 60.0F); // remembers (0, 0) as the previous transform
    moveEntityTo(scene, entity, {5.0F, 0.0F});
    expectNear(scene.worldTransform(entity, 0.0F).position, {5.0F, 0.0F}); // at alpha 0 too
}
