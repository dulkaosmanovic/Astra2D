#pragma once

// Which entity is under the cursor, for the overlay's pick mode. No OpenGL or ImGui, so it is
// unit-tested.

#include "astra2d/ecs/entity.hpp"

#include <glm/vec2.hpp>

namespace astra2d {
class Scene;
}

namespace astra2d::detail {

/// The entity whose selection outline (see selectionOutline()) contains @p point (world units),
/// among those with a SpriteRenderer or a Collider. If several do, the one drawn on top wins: the
/// higher SpriteRenderer::layer (entities without a sprite count as layer 0), then the smaller
/// box, so a coin in front of a large background is found. The null entity if there is none.
[[nodiscard]] Entity entityAt(const Scene& scene, glm::vec2 point, float alpha = 1.0F);

} // namespace astra2d::detail
