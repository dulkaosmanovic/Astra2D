#pragma once

// The rows of the entity list in EntityInspector, without ImGui, so that which entities are shown
// and in which order is unit-tested.

#include "astra2d/ecs/entity.hpp"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace astra2d {
class Registry;
class Scene;
} // namespace astra2d

namespace astra2d::detail {

/// One row of the entity list.
struct EntityRow {
    Entity entity;
    int depth = 0;            // indentation: 0 for a root
    bool hasChildren = false; // can be expanded
};

/// "Player #12" for an entity with a Name, "#12" without (the index; the generation is left out).
[[nodiscard]] std::string entityLabel(const Registry& registry, Entity entity);

/// Without a @p filter: every root in index order, each followed by its children (depth-first, in
/// child order) if it is in @p expanded. With a filter: every entity whose label contains it
/// (ignoring case), as a flat list.
[[nodiscard]] std::vector<EntityRow> entityRows(const Scene& scene,
                                                const std::unordered_set<Entity>& expanded,
                                                std::string_view filter = {});

} // namespace astra2d::detail
