// The rows of the entity list (entity_rows): which entities are listed, in which order and how
// deep, with and without a filter. No ImGui needed.

#include "debug/entity_rows.hpp"

#include <astra2d/scene/scene.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using astra2d::Entity;
using astra2d::Scene;
using astra2d::detail::entityLabel;
using astra2d::detail::EntityRow;
using astra2d::detail::entityRows;

namespace {

std::vector<Entity> entitiesOf(const std::vector<EntityRow>& rows)
{
    std::vector<Entity> entities;
    entities.reserve(rows.size());
    for (const EntityRow& row : rows) {
        entities.push_back(row.entity);
    }
    return entities;
}

} // namespace

TEST(EntityRowsTests, LabelHasNameAndIndex)
{
    Scene scene;
    const Entity named = scene.createEntity("player");
    const Entity anonymous = scene.createEntity();
    EXPECT_EQ(entityLabel(scene.registry(), named), "player #" + std::to_string(named.index()));
    EXPECT_EQ(entityLabel(scene.registry(), anonymous), "#" + std::to_string(anonymous.index()));
}

TEST(EntityRowsTests, CollapsedParentsListOnlyRoots)
{
    Scene scene;
    const Entity a = scene.createEntity("a");
    const Entity b = scene.createEntity("b");
    const Entity child = scene.createEntity("child");
    scene.setParent(child, a);
    const auto rows = entityRows(scene, {});
    EXPECT_EQ(entitiesOf(rows), (std::vector{a, b}));
    EXPECT_TRUE(rows[0].hasChildren);
    EXPECT_FALSE(rows[1].hasChildren);
}

TEST(EntityRowsTests, ExpandedParentsListChildrenDepthFirst)
{
    Scene scene;
    const Entity root = scene.createEntity("root");
    const Entity other = scene.createEntity("other");
    const Entity first = scene.createEntity("first");
    const Entity second = scene.createEntity("second");
    const Entity grandchild = scene.createEntity("grandchild");
    scene.setParent(first, root);
    scene.setParent(second, root);
    scene.setParent(grandchild, first);

    const auto rows = entityRows(scene, {root, first});
    EXPECT_EQ(entitiesOf(rows), (std::vector{root, first, grandchild, second, other}));
    EXPECT_EQ(rows[0].depth, 0);
    EXPECT_EQ(rows[1].depth, 1);
    EXPECT_EQ(rows[2].depth, 2);
    EXPECT_EQ(rows[3].depth, 1);
    EXPECT_EQ(rows[4].depth, 0);

    // A collapsed child hides its own children even when expanded further down.
    EXPECT_EQ(entitiesOf(entityRows(scene, {first})), (std::vector{root, other}));
}

TEST(EntityRowsTests, FilterListsMatchesFlatIgnoringCase)
{
    Scene scene;
    const Entity parent = scene.createEntity("Coin row");
    const Entity coin = scene.createEntity("coin");
    scene.createEntity("crawler");
    scene.setParent(coin, parent);
    const auto rows = entityRows(scene, {}, "COIN");
    EXPECT_EQ(entitiesOf(rows), (std::vector{parent, coin}));
    EXPECT_TRUE(std::ranges::all_of(rows, [](const EntityRow& r) { return r.depth == 0; }));
}

TEST(EntityRowsTests, FilterMatchesTheIndex)
{
    Scene scene;
    scene.createEntity();
    const Entity second = scene.createEntity();
    const auto rows = entityRows(scene, {}, "#" + std::to_string(second.index()));
    EXPECT_EQ(entitiesOf(rows), (std::vector{second}));
}
