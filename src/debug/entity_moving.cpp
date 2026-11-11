#include "entity_moving.hpp"

#include "astra2d/scene/components.hpp"
#include "astra2d/scene/scene.hpp"

namespace astra2d::detail {

void moveEntityTo(Scene& scene, Entity entity, glm::vec2 world)
{
    Registry& registry = scene.registry();
    auto& transform = registry.get<Transform>(entity);
    const Entity parent = scene.parent(entity);
    if (parent.isNull()) {
        transform.position = world;
    } else {
        Transform target = scene.worldTransform(entity);
        target.position = world;
        transform.position = relativeTo(scene.worldTransform(parent), target).position;
    }
    scene.resetInterpolation(entity);
    if (auto* body = registry.tryGet<RigidBody>(entity)) {
        body->velocity = glm::vec2(0.0F);
        body->angularVelocity = 0.0F;
    }
}

} // namespace astra2d::detail
