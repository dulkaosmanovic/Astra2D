// The box DebugOverlay::drawSelection() draws around the selected entity: from its sprite, its
// collider or, without either, a marker at its position.

#include "debug/selection_outline.hpp"

#include <astra2d/scene/components.hpp>
#include <astra2d/scene/scene.hpp>

#include <gtest/gtest.h>

#include <array>
#include <numbers>

using astra2d::Collider;
using astra2d::ColliderShape;
using astra2d::Entity;
using astra2d::Scene;
using astra2d::SpriteRenderer;
using astra2d::Transform;
using astra2d::detail::selectionOutline;

namespace {

void expectCorners(const std::array<glm::vec2, 4>& corners, glm::vec2 low, glm::vec2 high)
{
    const std::array<glm::vec2, 4> expected = {low, glm::vec2(high.x, low.y), high,
                                               glm::vec2(low.x, high.y)};
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_NEAR(corners[i].x, expected[i].x, 1e-4F) << i;
        EXPECT_NEAR(corners[i].y, expected[i].y, 1e-4F) << i;
    }
}

} // namespace

TEST(SelectionOutlineTests, SpriteSizeTimesScale)
{
    Scene scene;
    const Entity entity = scene.createEntity("sprite", {10.0F, 20.0F});
    scene.registry().get<Transform>(entity).scale = {2.0F, -1.0F}; // flipped: same box
    scene.registry().emplace<SpriteRenderer>(entity, SpriteRenderer{.size = {4.0F, 6.0F}});
    expectCorners(selectionOutline(scene, entity, 1.0F), {6.0F, 17.0F}, {14.0F, 23.0F});
}

TEST(SelectionOutlineTests, ColliderBoxAroundItsOffsetWithoutScale)
{
    Scene scene;
    const Entity entity = scene.createEntity("box", {0.0F, 0.0F});
    scene.registry().get<Transform>(entity).scale = {3.0F, 3.0F}; // colliders ignore it
    scene.registry().emplace<Collider>(
        entity, Collider{.halfExtents = {1.0F, 2.0F}, .offset = {5.0F, 0.0F}});
    expectCorners(selectionOutline(scene, entity, 1.0F), {4.0F, -2.0F}, {6.0F, 2.0F});
}

TEST(SelectionOutlineTests, CircleAndPolygonBounds)
{
    Scene scene;
    const Entity circle = scene.createEntity("circle", {1.0F, 1.0F});
    scene.registry().emplace<Collider>(circle,
                                       Collider{.shape = ColliderShape::Circle, .radius = 2.0F});
    expectCorners(selectionOutline(scene, circle, 1.0F), {-1.0F, -1.0F}, {3.0F, 3.0F});

    const Entity polygon = scene.createEntity("polygon");
    scene.registry().emplace<Collider>(
        polygon, Collider{.shape = ColliderShape::Polygon,
                          .points = {{0.0F, 0.0F}, {4.0F, 0.0F}, {2.0F, 3.0F}}});
    expectCorners(selectionOutline(scene, polygon, 1.0F), {0.0F, 0.0F}, {4.0F, 3.0F});
}

TEST(SelectionOutlineTests, MarkerWithoutSpriteOrCollider)
{
    Scene scene;
    const Entity entity = scene.createEntity("empty", {2.0F, 2.0F});
    expectCorners(selectionOutline(scene, entity, 0.5F), {1.5F, 1.5F}, {2.5F, 2.5F});
}

TEST(SelectionOutlineTests, RotatesWithTheEntity)
{
    Scene scene;
    const Entity entity = scene.createEntity("turned");
    scene.registry().get<Transform>(entity).rotation = std::numbers::pi_v<float> / 2.0F;
    scene.registry().emplace<SpriteRenderer>(entity, SpriteRenderer{.size = {4.0F, 2.0F}});
    const auto corners = selectionOutline(scene, entity, 1.0F);
    // A quarter turn: the 4 x 2 box stands 2 wide and 4 high; its first corner (-2, -1) goes to
    // (1, -2).
    EXPECT_NEAR(corners[0].x, 1.0F, 1e-4F);
    EXPECT_NEAR(corners[0].y, -2.0F, 1e-4F);
    EXPECT_NEAR(corners[2].x, -1.0F, 1e-4F);
    EXPECT_NEAR(corners[2].y, 2.0F, 1e-4F);
}
