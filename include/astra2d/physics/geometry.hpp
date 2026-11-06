#pragma once

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <span>

/// @addtogroup physics
/// @{

namespace astra2d {

/// The 2D cross product of two vectors (the z component of their 3D cross product).
[[nodiscard]] inline float cross(glm::vec2 a, glm::vec2 b)
{
    return (a.x * b.y) - (a.y * b.x);
}
/// Cross product of a scalar (an angular velocity around z) with a vector: `w × v`.
[[nodiscard]] inline glm::vec2 cross(float w, glm::vec2 v)
{
    return {-w * v.y, w * v.x};
}
/// Cross product of a vector with a scalar: `v × w`.
[[nodiscard]] inline glm::vec2 cross(glm::vec2 v, float w)
{
    return {w * v.y, -w * v.x};
}

/// A rotation as cosine and sine, so rotating a vector needs no trigonometry.
struct Rotation {
    float c = 1.0F; ///< Cosine of the angle.
    float s = 0.0F; ///< Sine of the angle.

    /// The rotation by @p angle radians, counter-clockwise.
    [[nodiscard]] static Rotation fromAngle(float angle)
    {
        return {.c = std::cos(angle), .s = std::sin(angle)};
    }
    /// True for the rotation by 0 (exactly), where boxes are axis-aligned.
    [[nodiscard]] bool isIdentity() const { return c == 1.0F && s == 0.0F; }
    /// Rotates @p v.
    [[nodiscard]] glm::vec2 apply(glm::vec2 v) const
    {
        return {(c * v.x) - (s * v.y), (s * v.x) + (c * v.y)};
    }
    /// Rotates @p v back (by the inverse rotation).
    [[nodiscard]] glm::vec2 applyInverse(glm::vec2 v) const
    {
        return {(c * v.x) + (s * v.y), (-s * v.x) + (c * v.y)};
    }
};

/// Position and rotation of a rigid body: maps a body's local coordinates to world coordinates.
struct Pose {
    glm::vec2 position{0.0F}; ///< The body's origin in world units.
    Rotation rotation;        ///< The body's rotation.

    /// A local point in world coordinates.
    [[nodiscard]] glm::vec2 apply(glm::vec2 local) const
    {
        return position + rotation.apply(local);
    }
    /// A world point in local coordinates.
    [[nodiscard]] glm::vec2 applyInverse(glm::vec2 world) const
    {
        return rotation.applyInverse(world - position);
    }
};

/// An axis-aligned bounding box.
struct Aabb {
    glm::vec2 min{0.0F}; ///< Lower-left corner.
    glm::vec2 max{0.0F}; ///< Upper-right corner.

    /// The centre.
    [[nodiscard]] glm::vec2 center() const { return 0.5F * (min + max); }
    /// Half the size.
    [[nodiscard]] glm::vec2 halfExtents() const { return 0.5F * (max - min); }
    /// True if the boxes overlap or touch.
    [[nodiscard]] bool overlaps(const Aabb& other) const
    {
        return min.x <= other.max.x && other.min.x <= max.x && min.y <= other.max.y &&
               other.min.y <= max.y;
    }
    /// True if @p point is inside or on the border.
    [[nodiscard]] bool contains(glm::vec2 point) const
    {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
    }
    /// The smallest box containing both.
    [[nodiscard]] Aabb merged(const Aabb& other) const
    {
        return {.min = glm::min(min, other.min), .max = glm::max(max, other.max)};
    }
    /// The box grown by @p margin on every side.
    [[nodiscard]] Aabb expanded(float margin) const
    {
        return {.min = min - margin, .max = max + margin};
    }
};

/// A circle in a body's local coordinates.
struct Circle {
    glm::vec2 center{0.0F}; ///< Centre relative to the body origin.
    float radius = 0.5F;    ///< Radius (> 0).
};

/// A convex polygon in a body's local coordinates, counter-clockwise, at most kMaxVertices corners.
/// Make one with makeBox() or makePolygon(); they compute the normals and centroid.
struct Polygon {
    /// The largest number of corners.
    static constexpr int kMaxVertices = 8;

    std::array<glm::vec2, kMaxVertices> vertices{}; ///< Corners, counter-clockwise.
    /// Outward unit normal of edge i, which goes from vertices[i] to vertices[i + 1].
    std::array<glm::vec2, kMaxVertices> normals{};
    int count = 0;            ///< Number of corners (3 to kMaxVertices).
    glm::vec2 centroid{0.0F}; ///< Centre of area.

    /// The corners in use.
    [[nodiscard]] std::span<const glm::vec2> points() const
    {
        return {vertices.data(), static_cast<std::size_t>(count)};
    }
};

// Edge bits of a box made by makeBox(), for ShapeDef::edgeMask: edge 0 is the bottom, then
// counter-clockwise. Plain constants rather than an enum, since they are combined with |.

/// Edge bit of a box made by makeBox(): the edge facing -y (edge 0).
inline constexpr unsigned kBoxBottom = 1U << 0U;
/// Edge bit of a box: the edge facing +x (edge 1).
inline constexpr unsigned kBoxRight = 1U << 1U;
/// Edge bit of a box: the edge facing +y (edge 2).
inline constexpr unsigned kBoxTop = 1U << 2U;
/// Edge bit of a box: the edge facing -x (edge 3).
inline constexpr unsigned kBoxLeft = 1U << 3U;

/// A box with half size @p halfExtents centred at @p center, rotated by @p angle around its centre.
/// Its corners start at the bottom-left, so edge 0 is the bottom one (see kBoxBottom).
/// @pre both half extents > 0.
[[nodiscard]] Polygon makeBox(glm::vec2 halfExtents, glm::vec2 center = glm::vec2(0.0F),
                              float angle = 0.0F);

/// The convex hull of @p points (any order). Points closer than a small tolerance are merged and
/// corners on a straight edge are dropped.
/// @throws std::invalid_argument if the hull has fewer than 3 corners (all points on a line) or
/// more than Polygon::kMaxVertices.
[[nodiscard]] Polygon makePolygon(std::span<const glm::vec2> points);

/// Area, centre of area and moment of inertia of a shape with density 1.
struct MassData {
    float area = 0.0F;      ///< Area.
    glm::vec2 center{0.0F}; ///< Centre of area, in the shape's coordinates.
    float inertia = 0.0F;   ///< Polar moment of inertia about @ref center.
};

/// Mass data of @p circle.
[[nodiscard]] MassData computeMass(const Circle& circle);
/// Mass data of @p polygon.
[[nodiscard]] MassData computeMass(const Polygon& polygon);

/// Bounding box of @p circle placed at @p pose.
[[nodiscard]] Aabb computeAabb(const Circle& circle, const Pose& pose);
/// Bounding box of @p polygon placed at @p pose.
[[nodiscard]] Aabb computeAabb(const Polygon& polygon, const Pose& pose);

} // namespace astra2d

/// @}
