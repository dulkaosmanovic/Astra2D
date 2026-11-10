#pragma once

// The built-in inspectors of EntityInspector: ImGui widgets for the engine's components.

namespace astra2d {
class EntityInspector;
}

namespace astra2d::detail {

/// Adds inspectors for Name, Transform, SpriteRenderer, Animator, Lifetime, RigidBody, Collider,
/// TilemapCollider, Relationship and Interpolated to @p inspector.
void addBuiltInInspectors(EntityInspector& inspector);

} // namespace astra2d::detail
