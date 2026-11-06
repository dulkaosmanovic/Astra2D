#include "astra2d/physics/geometry.hpp"

#include "astra2d/core/assert.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace astra2d {

namespace {

// Fills normals and centroid of a polygon whose vertices and count are set.
void finish(Polygon& polygon)
{
    for (int i = 0; i < polygon.count; ++i) {
        const glm::vec2 edge = polygon.vertices[static_cast<std::size_t>((i + 1) % polygon.count)] -
                               polygon.vertices[static_cast<std::size_t>(i)];
        polygon.normals[static_cast<std::size_t>(i)] = glm::normalize(glm::vec2(edge.y, -edge.x));
    }
    polygon.centroid = computeMass(polygon).center;
}

} // namespace

Polygon makeBox(glm::vec2 halfExtents, glm::vec2 center, float angle)
{
    ASTRA_ASSERT(halfExtents.x > 0.0F && halfExtents.y > 0.0F, "a box needs a positive size");
    Polygon box;
    box.count = 4;
    const Rotation rotation = Rotation::fromAngle(angle);
    const std::array<glm::vec2, 4> corners = {
        glm::vec2(-halfExtents.x, -halfExtents.y), glm::vec2(halfExtents.x, -halfExtents.y),
        glm::vec2(halfExtents.x, halfExtents.y), glm::vec2(-halfExtents.x, halfExtents.y)};
    const std::array<glm::vec2, 4> normals = {glm::vec2(0.0F, -1.0F), glm::vec2(1.0F, 0.0F),
                                              glm::vec2(0.0F, 1.0F), glm::vec2(-1.0F, 0.0F)};
    for (std::size_t i = 0; i < 4; ++i) {
        box.vertices[i] = center + rotation.apply(corners[i]);
        box.normals[i] = rotation.apply(normals[i]);
    }
    box.centroid = center;
    return box;
}

Polygon makePolygon(std::span<const glm::vec2> points)
{
    if (points.size() < 3) {
        throw std::invalid_argument("a polygon needs at least 3 points");
    }
    // Weld points closer than a tolerance relative to the polygon's size.
    glm::vec2 lower = points[0];
    glm::vec2 upper = points[0];
    for (const glm::vec2 p : points) {
        lower = glm::min(lower, p);
        upper = glm::max(upper, p);
    }
    const float size = std::max(upper.x - lower.x, upper.y - lower.y);
    const float weld = 1e-4F * size;

    std::vector<glm::vec2> unique;
    for (const glm::vec2 p : points) {
        const bool duplicate = std::ranges::any_of(
            unique, [&](glm::vec2 q) { return glm::dot(p - q, p - q) < weld * weld; });
        if (!duplicate) {
            unique.push_back(p);
        }
    }

    // Andrew's monotone chain; collinear points are dropped (cross <= tolerance).
    std::ranges::sort(
        unique, [](glm::vec2 a, glm::vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    const float tolerance = 1e-6F * size * size;
    std::vector<glm::vec2> hull(2 * unique.size());
    std::size_t k = 0;
    for (const glm::vec2 p : unique) {
        while (k >= 2 && cross(hull[k - 1] - hull[k - 2], p - hull[k - 2]) <= tolerance) {
            --k;
        }
        hull[k++] = p;
    }
    const std::size_t lowerSize = k + 1;
    for (auto it = unique.rbegin() + 1; it != unique.rend(); ++it) {
        while (k >= lowerSize && cross(hull[k - 1] - hull[k - 2], *it - hull[k - 2]) <= tolerance) {
            --k;
        }
        hull[k++] = *it;
    }
    const std::size_t count = k > 0 ? k - 1 : 0; // the last point repeats the first
    if (count < 3) {
        throw std::invalid_argument("polygon points lie on a line");
    }
    if (count > static_cast<std::size_t>(Polygon::kMaxVertices)) {
        throw std::invalid_argument("a polygon has at most 8 corners");
    }

    Polygon polygon;
    polygon.count = static_cast<int>(count);
    std::copy_n(hull.begin(), count, polygon.vertices.begin());
    finish(polygon);
    return polygon;
}

MassData computeMass(const Circle& circle)
{
    const float rr = circle.radius * circle.radius;
    const float area = std::numbers::pi_v<float> * rr;
    return {.area = area, .center = circle.center, .inertia = 0.5F * area * rr};
}

MassData computeMass(const Polygon& polygon)
{
    ASTRA_ASSERT(polygon.count >= 3, "a polygon needs at least 3 corners");
    // Triangle fan from the first corner (closer than the origin, so less rounding).
    const glm::vec2 origin = polygon.vertices[0];
    float area = 0.0F;
    glm::vec2 center(0.0F);
    float inertia = 0.0F;
    for (int i = 1; i + 1 < polygon.count; ++i) {
        const glm::vec2 e1 = polygon.vertices[static_cast<std::size_t>(i)] - origin;
        const glm::vec2 e2 = polygon.vertices[static_cast<std::size_t>(i) + 1] - origin;
        const float d = cross(e1, e2);
        const float triangleArea = 0.5F * d;
        area += triangleArea;
        center += triangleArea * (e1 + e2) / 3.0F;
        const float intX2 = (e1.x * e1.x) + (e2.x * e1.x) + (e2.x * e2.x);
        const float intY2 = (e1.y * e1.y) + (e2.y * e1.y) + (e2.y * e2.y);
        inertia += (0.25F / 3.0F) * d * (intX2 + intY2);
    }
    ASTRA_ASSERT(area > 0.0F, "polygon corners must be counter-clockwise");
    center /= area;
    // inertia is about the first corner; move it to the centre of area.
    return {.area = area,
            .center = origin + center,
            .inertia = inertia - (area * glm::dot(center, center))};
}

Aabb computeAabb(const Circle& circle, const Pose& pose)
{
    const glm::vec2 center = pose.apply(circle.center);
    return {.min = center - circle.radius, .max = center + circle.radius};
}

Aabb computeAabb(const Polygon& polygon, const Pose& pose)
{
    glm::vec2 lower = pose.apply(polygon.vertices[0]);
    glm::vec2 upper = lower;
    for (int i = 1; i < polygon.count; ++i) {
        const glm::vec2 p = pose.apply(polygon.vertices[static_cast<std::size_t>(i)]);
        lower = glm::min(lower, p);
        upper = glm::max(upper, p);
    }
    return {.min = lower, .max = upper};
}

} // namespace astra2d
