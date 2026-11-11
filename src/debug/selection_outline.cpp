#include "selection_outline.hpp"

#include "astra2d/physics/geometry.hpp"
#include "astra2d/scene/components.hpp"
#include "astra2d/scene/scene.hpp"

#include <glm/common.hpp>

#include <algorithm>

namespace astra2d::detail {

namespace {

// The local box (centre and half size) that stands for the entity.
struct LocalBox {
    glm::vec2 center{0.0F};
    glm::vec2 half{0.0F};
};

LocalBox colliderBox(const Collider& collider)
{
    switch (collider.shape) {
    case ColliderShape::Box:
        return {.center = collider.offset, .half = collider.halfExtents};
    case ColliderShape::Circle:
        return {.center = collider.offset, .half = glm::vec2(collider.radius)};
    case ColliderShape::Polygon:
        break;
    }
    if (collider.points.empty()) {
        return {.center = collider.offset};
    }
    glm::vec2 low = collider.points.front();
    glm::vec2 high = low;
    for (const glm::vec2 point : collider.points) {
        low = glm::min(low, point);
        high = glm::max(high, point);
    }
    return {.center = collider.offset + (0.5F * (low + high)), .half = 0.5F * (high - low)};
}

} // namespace

std::array<glm::vec2, 4> selectionOutline(const Scene& scene, Entity entity, float markerHalfSize,
                                          float alpha)
{
    const Registry& registry = scene.registry();
    const Transform world = scene.worldTransform(entity, alpha);
    LocalBox box{.half = glm::vec2(markerHalfSize)};
    bool scaled = false; // collider sizes ignore Transform::scale; sprite sizes do not
    if (const auto* sprite = registry.tryGet<SpriteRenderer>(entity)) {
        box = {.half = 0.5F * sprite->size};
        scaled = true;
    } else if (const auto* collider = registry.tryGet<Collider>(entity)) {
        box = colliderBox(*collider);
    }
    const glm::vec2 scale = scaled ? glm::abs(world.scale) : glm::vec2(1.0F);
    const Pose pose{.position = world.position, .rotation = Rotation::fromAngle(world.rotation)};
    const glm::vec2 c = box.center;
    const glm::vec2 h = box.half * scale;
    return {pose.apply(c + glm::vec2(-h.x, -h.y)), pose.apply(c + glm::vec2(h.x, -h.y)),
            pose.apply(c + glm::vec2(h.x, h.y)), pose.apply(c + glm::vec2(-h.x, h.y))};
}

} // namespace astra2d::detail
