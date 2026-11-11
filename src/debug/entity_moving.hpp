#pragma once

// Moving an entity with the mouse in the overlay's move mode. No OpenGL or ImGui, so it is
// unit-tested.

#include "astra2d/ecs/entity.hpp"

#include <glm/vec2.hpp>

namespace astra2d {
class Scene;
}

namespace astra2d::detail {

/// Moves @p entity so that its world position is @p world, keeping its rotation and scale. A
/// child's Transform is relative to its parent, so the position is converted into the parent's
/// space. The entity is drawn there at once (no interpolated slide), and a RigidBody stops moving,
/// so gravity does not pull it away from the mouse while it is held.
/// @pre the entity is valid.
void moveEntityTo(Scene& scene, Entity entity, glm::vec2 world);

} // namespace astra2d::detail
