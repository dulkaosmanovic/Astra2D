#include "entity_picking.hpp"

#include "astra2d/scene/components.hpp"
#include "astra2d/scene/scene.hpp"
#include "selection_outline.hpp"

#include <glm/geometric.hpp>

#include <array>
#include <limits>

namespace astra2d::detail {

namespace {

float cross(glm::vec2 a, glm::vec2 b)
{
    return (a.x * b.y) - (a.y * b.x);
}

// The corners are counter-clockwise (or degenerate), so the point is inside when it is on the
// left of, or on, every edge.
bool contains(const std::array<glm::vec2, 4>& corners, glm::vec2 point)
{
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const glm::vec2 edge = corners[(i + 1) % corners.size()] - corners[i];
        if (cross(edge, point - corners[i]) < 0.0F) {
            return false;
        }
    }
    return true;
}

float area(const std::array<glm::vec2, 4>& corners)
{
    return glm::length(corners[1] - corners[0]) * glm::length(corners[3] - corners[0]);
}

} // namespace

Entity entityAt(const Scene& scene, glm::vec2 point, float alpha)
{
    const Registry& registry = scene.registry();
    Entity best = kNullEntity;
    int bestLayer = std::numeric_limits<int>::min();
    float bestArea = std::numeric_limits<float>::max();
    registry.forEach([&](Entity entity) {
        const auto* sprite = registry.tryGet<SpriteRenderer>(entity);
        if (sprite == nullptr && !registry.has<Collider>(entity)) {
            return; // nothing visible to click on
        }
        const auto corners = selectionOutline(scene, entity, 0.0F, alpha);
        if (!contains(corners, point)) {
            return;
        }
        const int layer = sprite != nullptr ? sprite->layer : 0;
        const float size = area(corners);
        if (layer > bestLayer || (layer == bestLayer && size < bestArea)) {
            best = entity;
            bestLayer = layer;
            bestArea = size;
        }
    });
    return best;
}

} // namespace astra2d::detail
