// Picking an entity with the mouse in the overlay's pick mode: which entity is under a point.

#include "debug/entity_picking.hpp"

#include <astra2d/scene/components.hpp>
#include <astra2d/scene/scene.hpp>

#include <gtest/gtest.h>

#include <numbers>

using astra2d::Collider;
using astra2d::Entity;
using astra2d::Scene;
using astra2d::SpriteRenderer;
using astra2d::Transform;
using astra2d::detail::entityAt;

namespace {

Entity addSprite(Scene& scene, glm::vec2 position, glm::vec2 size, int layer = 0)
{
    const Entity entity = scene.createEntity("sprite", position);
    scene.registry().emplace<SpriteRenderer>(entity, SpriteRenderer{.size = size, .layer = layer});
    return entity;
}

} // namespace

TEST(EntityPickingTests, FindsTheSpriteUnderThePoint)
{
    Scene scene;
    const Entity a = addSprite(scene, {0.0F, 0.0F}, {2.0F, 2.0F});
    const Entity b = addSprite(scene, {10.0F, 0.0F}, {2.0F, 2.0F});
    EXPECT_EQ(entityAt(scene, {0.5F, -0.9F}), a);
    EXPECT_EQ(entityAt(scene, {10.0F, 0.0F}), b);
    EXPECT_TRUE(entityAt(scene, {5.0F, 0.0F}).isNull());
}

TEST(EntityPickingTests, HigherLayerWins)
{
    Scene scene;
    addSprite(scene, {0.0F, 0.0F}, {2.0F, 2.0F}, 0);
    const Entity front = addSprite(scene, {0.0F, 0.0F}, {8.0F, 8.0F}, 5);
    EXPECT_EQ(entityAt(scene, {0.0F, 0.0F}), front); // larger, but drawn on top
}

TEST(EntityPickingTests, SmallerBoxWinsOnTheSameLayer)
{
    Scene scene;
    addSprite(scene, {0.0F, 0.0F}, {100.0F, 100.0F}); // a background
    const Entity coin = addSprite(scene, {3.0F, 3.0F}, {1.0F, 1.0F});
    EXPECT_EQ(entityAt(scene, {3.0F, 3.0F}), coin);
}

TEST(EntityPickingTests, CollidersCountAndEmptyEntitiesDoNot)
{
    Scene scene;
    scene.createEntity("empty", {0.0F, 0.0F}); // a bare Transform: nothing to click
    const Entity wall = scene.createEntity("wall", {0.0F, 0.0F});
    scene.registry().emplace<Collider>(wall, Collider{.halfExtents = {1.0F, 3.0F}});
    EXPECT_EQ(entityAt(scene, {0.0F, 2.5F}), wall);
    EXPECT_TRUE(entityAt(scene, {1.5F, 0.0F}).isNull());
}

TEST(EntityPickingTests, RotatedBoxes)
{
    Scene scene;
    const Entity stick = addSprite(scene, {0.0F, 0.0F}, {6.0F, 1.0F});
    scene.registry().get<Transform>(stick).rotation = std::numbers::pi_v<float> / 2.0F;
    EXPECT_EQ(entityAt(scene, {0.0F, 2.5F}), stick); // now standing up
    EXPECT_TRUE(entityAt(scene, {2.5F, 0.0F}).isNull());
}
