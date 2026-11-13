#pragma once

#include "astra2d/physics/geometry.hpp"

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <optional>

/// @addtogroup physics
/// @{

namespace astra2d {

/// One contact point of a Manifold.
struct ManifoldPoint {
    glm::vec2 point{0.0F}; ///< World position, halfway between the two surfaces.
    /// Distance between the surfaces along the normal: negative when they overlap (penetration
    /// depth), positive for a speculative point that is not touching yet.
    float separation = 0.0F;
    /// Identifies the features (edges, corners) that made the point, so the solver can find the
    /// same point in the next step and start from its last impulses (warm starting).
    std::uint32_t id = 0;
    float normalImpulse = 0.0F;  ///< Impulse along the normal in the last step (set by the world).
    float tangentImpulse = 0.0F; ///< Friction impulse in the last step (set by the world).
};

/// Where and how deeply two shapes touch: the result of the narrow phase.
///
/// Two convex shapes touch at a point (corner on an edge, circles) or along a segment (edge on
/// edge), so two points are enough; for a segment they are its ends.
struct Manifold {
    glm::vec2 normal{0.0F, 1.0F};          ///< Unit vector from shape A to shape B.
    std::array<ManifoldPoint, 2> points{}; ///< Contact points; the first @ref count are used.
    int count = 0;                         ///< 0 (no contact), 1 or 2.

    /// The smallest separation of the used points, or a large number without points.
    [[nodiscard]] float minSeparation() const;
};

// All functions return a manifold whose normal points from the first shape to the second, with
// points whose separation is at most `margin` (0: only overlapping shapes; > 0 also returns
// speculative points of shapes that are less than `margin` apart).
//
// `edgeMask` disables edges of a box or polygon: bit i stands for edge i (kBoxBottom etc. for
// boxes). A disabled edge never gives the normal: used for the internal edges between tiles, which
// an object sliding along a wall or floor must not catch on. The shapes still only touch if they
// really overlap.

/// Two axis-aligned boxes: the normal is the axis of smallest penetration.
[[nodiscard]] Manifold collideAabbs(const Aabb& a, const Aabb& b, float margin = 0.0F,
                                    unsigned edgeMaskA = 0xFFU, unsigned edgeMaskB = 0xFFU);

/// Two circles, in world coordinates.
[[nodiscard]] Manifold collideCircles(const Circle& a, const Circle& b, float margin = 0.0F);

/// An axis-aligned box and a circle, in world coordinates.
[[nodiscard]] Manifold collideAabbCircle(const Aabb& box, const Circle& circle, float margin = 0.0F,
                                         unsigned edgeMask = 0xFFU);

/// Two convex polygons placed at their poses: separating axis test, then the incident edge is
/// clipped against the reference edge to get up to two points. @p lengthUnitsPerMeter scales the
/// tolerance that keeps the reference face from flipping between nearly equal faces (see
/// PhysicsSettings::lengthUnitsPerMeter).
[[nodiscard]] Manifold collidePolygons(const Polygon& a, const Pose& poseA, const Polygon& b,
                                       const Pose& poseB, float margin = 0.0F,
                                       unsigned edgeMaskA = 0xFFU, unsigned edgeMaskB = 0xFFU,
                                       float lengthUnitsPerMeter = 1.0F);

/// A convex polygon and a circle placed at their poses.
[[nodiscard]] Manifold collidePolygonCircle(const Polygon& polygon, const Pose& polygonPose,
                                            const Circle& circle, const Pose& circlePose,
                                            float margin = 0.0F, unsigned edgeMask = 0xFFU);

/// A ray from @p origin to `origin + translation`.
struct RayCastInput {
    glm::vec2 origin{0.0F};      ///< Start, in world units.
    glm::vec2 translation{0.0F}; ///< Direction and length.
};

/// Where a ray hits a shape.
struct RayCastHit {
    glm::vec2 point{0.0F};  ///< Hit point.
    glm::vec2 normal{0.0F}; ///< Surface normal at the hit point, facing the ray's origin.
    float fraction = 0.0F;  ///< 0 at the origin, 1 at the end of the ray.
};

/// The first hit of @p ray on @p circle placed at @p pose; none if the ray starts inside.
[[nodiscard]] std::optional<RayCastHit> rayCast(const RayCastInput& ray, const Circle& circle,
                                                const Pose& pose);
/// The first hit of @p ray on @p polygon placed at @p pose; none if the ray starts inside.
[[nodiscard]] std::optional<RayCastHit> rayCast(const RayCastInput& ray, const Polygon& polygon,
                                                const Pose& pose);

} // namespace astra2d

/// @}
