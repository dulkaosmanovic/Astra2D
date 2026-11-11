#pragma once

// Where the selected entity is in the world, for DebugOverlay::drawSelection(): a box around what
// the player sees of it. No OpenGL, so it is unit-tested.

#include "astra2d/ecs/entity.hpp"

#include <glm/vec2.hpp>

#include <array>

namespace astra2d {
class Scene;
}

namespace astra2d::detail {

/// The corners (counter-clockwise) of a box around @p entity at its world transform: its sprite
/// (SpriteRenderer::size times the scale, rotated), else its collider (box, the circle's or the
/// polygon's bounds, around Collider::offset), else a square of half size @p markerHalfSize at its
/// position.
/// @pre the entity is valid.
[[nodiscard]] std::array<glm::vec2, 4> selectionOutline(const Scene& scene, Entity entity,
                                                        float markerHalfSize, float alpha = 1.0F);

} // namespace astra2d::detail
