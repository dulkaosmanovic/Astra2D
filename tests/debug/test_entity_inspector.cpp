// The entity inspector: selection, the game's inspectors, and the widgets of every built-in
// component (component_widgets), run in a headless ImGui context.

#include "headless_imgui.hpp"

#include <astra2d/debug/entity_inspector.hpp>
#include <astra2d/scene/components.hpp>
#include <astra2d/scene/scene.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using astra2d::Entity;
using astra2d::EntityInspector;
using astra2d::Scene;

namespace {

struct Coin {
    int value = 1;
};

} // namespace

TEST(EntityInspectorTests, HasTheEngineComponentsBuiltIn)
{
    const EntityInspector inspector;
    const auto names = inspector.componentNames();
    for (const char* name :
         {"Name", "Transform", "Sprite renderer", "Animator", "Lifetime", "Rigid body", "Collider",
          "Tilemap collider", "Relationship", "Interpolated"}) {
        EXPECT_NE(std::ranges::find(names, name), names.end()) << name;
    }
}

TEST(EntityInspectorTests, GameInspectorRunsOnlyForEntitiesWithTheComponent)
{
    Scene scene;
    const Entity withCoin = scene.createEntity("coin");
    scene.registry().emplace<Coin>(withCoin, Coin{.value = 5});
    const Entity without = scene.createEntity("rock");

    EntityInspector inspector;
    int inspected = 0;
    int seenValue = 0;
    inspector.addInspector<Coin>("Coin", [&](Coin& coin) {
        ++inspected;
        seenValue = coin.value;
    });
    EXPECT_EQ(inspector.componentNames().back(), "Coin");

    HeadlessImGui imgui;
    inspector.select(without);
    imgui.frame([&] { inspector.draw(scene); });
    EXPECT_EQ(inspected, 0);
    inspector.select(withCoin);
    imgui.frame([&] { inspector.draw(scene); });
    EXPECT_EQ(inspected, 1);
    EXPECT_EQ(seenValue, 5);
}

TEST(EntityInspectorTests, DestroyedSelectionIsCleared)
{
    Scene scene;
    const Entity entity = scene.createEntity("doomed");
    EntityInspector inspector;
    inspector.select(entity);
    scene.destroyEntity(entity);

    HeadlessImGui imgui;
    imgui.frame([&] { inspector.draw(scene); });
    EXPECT_TRUE(inspector.selected().isNull());
}

// Every built-in widget once, with the hierarchy, a filter and several frames: ImGui's assertions
// (unbalanced IDs, Begin/End, tables) would abort the test.
TEST(EntityInspectorTests, DrawsEveryBuiltInComponent)
{
    using namespace astra2d;
    Scene scene;
    const Entity parent = scene.createEntity("parent");
    const Entity entity = scene.createEntity("everything", {1.0F, 2.0F});
    scene.setParent(entity, parent);
    Registry& registry = scene.registry();
    registry.emplace<SpriteRenderer>(entity);
    registry.emplace<Animator>(entity);
    registry.emplace<Lifetime>(entity, Lifetime{.duration = 2.0F, .age = 0.5F});
    registry.emplace<RigidBody>(entity);
    registry.emplace<Collider>(entity, Collider{.shape = ColliderShape::Circle});
    registry.emplace<TilemapCollider>(entity);
    registry.emplace<Interpolated>(entity);
    for (int i = 0; i < 500; ++i) { // more rows than the list shows: the clipper skips them
        scene.createEntity("filler");
    }

    EntityInspector inspector;
    inspector.select(entity);
    HeadlessImGui imgui;
    for (int frame = 0; frame < 3; ++frame) {
        imgui.frame([&] { inspector.draw(scene); });
    }
    EXPECT_EQ(inspector.selected(), entity);
}
