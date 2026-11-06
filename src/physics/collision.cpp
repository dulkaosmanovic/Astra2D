#include "astra2d/physics/collision.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace astra2d {

namespace {

constexpr float kHuge = std::numeric_limits<float>::max();

// Feature id tags, so ids from different collision functions never match by accident.
constexpr std::uint32_t kTagAabb = 1U << 28U;
constexpr std::uint32_t kTagPolygon = 2U << 28U;
constexpr std::uint32_t kTagPolygonCircle = 3U << 28U;

bool edgeEnabled(unsigned bits, int index)
{
    return ((bits >> static_cast<unsigned>(index)) & 1U) != 0U;
}

// Outward normals of the edges of an axis-aligned box, in makeBox() order.
constexpr std::array<glm::vec2, 4> kBoxNormals = {glm::vec2(0.0F, -1.0F), glm::vec2(1.0F, 0.0F),
                                                  glm::vec2(0.0F, 1.0F), glm::vec2(-1.0F, 0.0F)};

// A polygon moved to world coordinates.
struct WorldPolygon {
    std::array<glm::vec2, Polygon::kMaxVertices> vertices{};
    std::array<glm::vec2, Polygon::kMaxVertices> normals{};
    int count = 0;

    [[nodiscard]] glm::vec2 vertex(int i) const
    {
        return vertices[static_cast<std::size_t>(i % count)];
    }
    [[nodiscard]] glm::vec2 normal(int i) const { return normals[static_cast<std::size_t>(i)]; }
};

WorldPolygon toWorld(const Polygon& polygon, const Pose& pose)
{
    WorldPolygon result;
    result.count = polygon.count;
    for (int i = 0; i < polygon.count; ++i) {
        const auto k = static_cast<std::size_t>(i);
        result.vertices[k] = pose.apply(polygon.vertices[k]);
        result.normals[k] = pose.rotation.apply(polygon.normals[k]);
    }
    return result;
}

struct EdgeSeparation {
    int edge = -1;
    float separation = -kHuge;
};

// The edge of `a` (among the enabled ones) along whose normal `b` is farthest away: the
// separating axis test with the face normals of `a`.
EdgeSeparation maxSeparation(const WorldPolygon& a, const WorldPolygon& b, unsigned mask)
{
    EdgeSeparation best;
    for (int i = 0; i < a.count; ++i) {
        if (!edgeEnabled(mask, i)) {
            continue;
        }
        const glm::vec2 n = a.normal(i);
        const glm::vec2 v = a.vertex(i);
        float separation = kHuge;
        for (int j = 0; j < b.count; ++j) {
            separation = std::min(separation, glm::dot(n, b.vertex(j) - v));
        }
        if (separation > best.separation) {
            best = {.edge = i, .separation = separation};
        }
    }
    return best;
}

struct ClipVertex {
    glm::vec2 point{0.0F};
    std::uint32_t id = 0;
};

// Keeps the part of segment `in` with dot(normal, p) <= offset. Returns the number of points out.
int clipSegment(const std::array<ClipVertex, 2>& in, std::array<ClipVertex, 2>& out,
                glm::vec2 normal, float offset)
{
    int count = 0;
    const float d0 = glm::dot(normal, in[0].point) - offset;
    const float d1 = glm::dot(normal, in[1].point) - offset;
    if (d0 <= 0.0F) {
        out[static_cast<std::size_t>(count++)] = in[0];
    }
    if (d1 <= 0.0F) {
        out[static_cast<std::size_t>(count++)] = in[1];
    }
    if (d0 * d1 < 0.0F) {
        // The segment crosses the line: add the intersection, with the id of the clipped end.
        const float t = d0 / (d0 - d1);
        out[static_cast<std::size_t>(count)].point = in[0].point + t * (in[1].point - in[0].point);
        out[static_cast<std::size_t>(count)].id = d0 > 0.0F ? in[0].id : in[1].id;
        ++count;
    }
    return count;
}

} // namespace

float Manifold::minSeparation() const
{
    float separation = kHuge;
    for (int i = 0; i < count; ++i) {
        separation = std::min(separation, points[static_cast<std::size_t>(i)].separation);
    }
    return separation;
}

Manifold collideAabbs(const Aabb& a, const Aabb& b, float margin, unsigned edgeMaskA,
                      unsigned edgeMaskB)
{
    // Separation along each axis; the normal points from a to b. A face of `a` touches the
    // opposite face of `b`: both must be enabled for the axis to give the normal.
    struct Axis {
        glm::vec2 normal;
        float separation;
        int edgeA;
        int edgeB;
    };
    const std::array<Axis, 4> axes = {{
        {.normal = {0.0F, 1.0F}, .separation = b.min.y - a.max.y, .edgeA = 2, .edgeB = 0},
        {.normal = {0.0F, -1.0F}, .separation = a.min.y - b.max.y, .edgeA = 0, .edgeB = 2},
        {.normal = {1.0F, 0.0F}, .separation = b.min.x - a.max.x, .edgeA = 1, .edgeB = 3},
        {.normal = {-1.0F, 0.0F}, .separation = a.min.x - b.max.x, .edgeA = 3, .edgeB = 1},
    }};
    float realSeparation = -kHuge;
    const Axis* best = nullptr;
    for (const Axis& axis : axes) {
        realSeparation = std::max(realSeparation, axis.separation);
        if (edgeEnabled(edgeMaskA, axis.edgeA) && edgeEnabled(edgeMaskB, axis.edgeB) &&
            (best == nullptr || axis.separation > best->separation)) {
            best = &axis;
        }
    }
    Manifold manifold;
    if (realSeparation > margin || best == nullptr) {
        return manifold;
    }
    manifold.normal = best->normal;
    const auto axisIndex = static_cast<std::uint32_t>(best - axes.data());

    // The contact segment: the overlap of both boxes along the face, halfway between the faces.
    const bool vertical = best->normal.x == 0.0F;
    const int along = vertical ? 0 : 1;
    const float low = std::max(a.min[along], b.min[along]);
    const float high = std::min(a.max[along], b.max[along]);
    const int across = 1 - along;
    const float faceA = best->normal[across] > 0.0F ? a.max[across] : a.min[across];
    const float faceB = best->normal[across] > 0.0F ? b.min[across] : b.max[across];
    const float middle = 0.5F * (faceA + faceB);

    const auto makePoint = [&](float position, std::uint32_t index) {
        ManifoldPoint point;
        point.point[along] = position;
        point.point[across] = middle;
        point.separation = best->separation;
        point.id = kTagAabb | (axisIndex << 8U) | index;
        return point;
    };
    if (high - low > 1e-6F * std::max(1.0F, std::abs(high))) {
        manifold.points[0] = makePoint(low, 0);
        manifold.points[1] = makePoint(high, 1);
        manifold.count = 2;
    } else {
        // Corner on corner (or a speculative contact of diagonal neighbours).
        manifold.points[0] = makePoint(0.5F * (low + high), 0);
        manifold.count = 1;
    }
    return manifold;
}

Manifold collideCircles(const Circle& a, const Circle& b, float margin)
{
    Manifold manifold;
    const glm::vec2 d = b.center - a.center;
    const float distance = glm::length(d);
    const float separation = distance - a.radius - b.radius;
    if (separation > margin) {
        return manifold;
    }
    manifold.normal = distance > 1e-9F ? d / distance : glm::vec2(0.0F, 1.0F);
    const glm::vec2 surfaceA = a.center + (a.radius * manifold.normal);
    const glm::vec2 surfaceB = b.center - (b.radius * manifold.normal);
    manifold.points[0] = {.point = 0.5F * (surfaceA + surfaceB), .separation = separation, .id = 0};
    manifold.count = 1;
    return manifold;
}

namespace {

// A circle (centre c, radius r) touching the face `edge` of a box, `distance` from it along the
// face's normal (negative: the centre is inside).
Manifold boxFaceContact(glm::vec2 c, float r, int edge, float distance, float margin)
{
    Manifold manifold;
    const glm::vec2 n = kBoxNormals.at(static_cast<std::size_t>(edge));
    const float separation = distance - r;
    if (separation > margin) {
        return manifold;
    }
    manifold.normal = n;
    // Halfway between the face (c - n * distance) and the circle's deepest point (c - n * r).
    manifold.points[0] = {.point = c - (0.5F * (distance + r) * n),
                          .separation = separation,
                          .id = kTagAabb | (static_cast<std::uint32_t>(edge) << 8U) | 0x80U};
    manifold.count = 1;
    return manifold;
}

// The circle's centre is inside the box: push it out through the nearest enabled face.
Manifold centreInsideBox(const Aabb& box, glm::vec2 c, float r, float margin, unsigned edgeMask)
{
    const std::array<float, 4> distances = {c.y - box.min.y, box.max.x - c.x, box.max.y - c.y,
                                            c.x - box.min.x};
    int best = -1;
    for (int edge = 0; edge < 4; ++edge) {
        const auto k = static_cast<std::size_t>(edge);
        if (edgeEnabled(edgeMask, edge) &&
            (best < 0 || distances[k] < distances[static_cast<std::size_t>(best)])) {
            best = edge;
        }
    }
    return best < 0
               ? Manifold{}
               : boxFaceContact(c, r, best, -distances[static_cast<std::size_t>(best)], margin);
}

} // namespace

Manifold collideAabbCircle(const Aabb& box, const Circle& circle, float margin, unsigned edgeMask)
{
    const glm::vec2 c = circle.center;
    const float r = circle.radius;
    const bool outsideX = c.x < box.min.x || c.x > box.max.x;
    const bool outsideY = c.y < box.min.y || c.y > box.max.y;
    if (!outsideX && !outsideY) {
        return centreInsideBox(box, c, r, margin, edgeMask);
    }

    const glm::vec2 closest = glm::clamp(c, box.min, box.max);
    const glm::vec2 d = c - closest;
    const float distance = glm::length(d);
    if (distance - r > margin) {
        return {};
    }
    // The faces the centre is outside of (edge order as in makeBox(): bottom, right, top, left).
    const int edgeX = c.x > box.max.x ? 1 : 3;
    const int edgeY = c.y > box.max.y ? 2 : 0;
    const float distanceX = c.x > box.max.x ? c.x - box.max.x : box.min.x - c.x;
    const float distanceY = c.y > box.max.y ? c.y - box.max.y : box.min.y - c.y;
    const bool useX = outsideX && edgeEnabled(edgeMask, edgeX);
    const bool useY = outsideY && edgeEnabled(edgeMask, edgeY);
    if (outsideX && outsideY && useX && useY) {
        // Corner region: the normal goes from the corner to the centre.
        Manifold manifold;
        manifold.normal = d / distance;
        manifold.points[0] = {.point = 0.5F * (closest + c - (r * manifold.normal)),
                              .separation = distance - r,
                              .id = kTagAabb | 0x40U};
        manifold.count = 1;
        return manifold;
    }
    // A face region, or a corner one of whose edges is disabled: then the corner belongs to the
    // flat face of a neighbouring tile and acts as part of the other face.
    if (useX) {
        return boxFaceContact(c, r, edgeX, distanceX, margin);
    }
    if (useY) {
        return boxFaceContact(c, r, edgeY, distanceY, margin);
    }
    return {};
}

Manifold collidePolygons(const Polygon& a, const Pose& poseA, const Polygon& b, const Pose& poseB,
                         float margin, unsigned edgeMaskA, unsigned edgeMaskB)
{
    Manifold manifold;
    const WorldPolygon worldA = toWorld(a, poseA);
    const WorldPolygon worldB = toWorld(b, poseB);

    // Separating axis test over all face normals: separated along any of them, no contact.
    const EdgeSeparation allA = maxSeparation(worldA, worldB, 0xFFU);
    if (allA.separation > margin) {
        return manifold;
    }
    const EdgeSeparation allB = maxSeparation(worldB, worldA, 0xFFU);
    if (allB.separation > margin) {
        return manifold;
    }
    // The reference face: the enabled face with the largest separation. Prefer `a` unless `b` is
    // clearly better, so the choice does not flip between steps for nearly equal values.
    const EdgeSeparation sepA =
        edgeMaskA == 0xFFU ? allA : maxSeparation(worldA, worldB, edgeMaskA);
    const EdgeSeparation sepB =
        edgeMaskB == 0xFFU ? allB : maxSeparation(worldB, worldA, edgeMaskB);
    if (sepA.edge < 0 && sepB.edge < 0) {
        return manifold;
    }
    constexpr float kTolerance = 0.1F * 0.005F;
    const bool flip =
        sepA.edge < 0 || (sepB.edge >= 0 && sepB.separation > sepA.separation + kTolerance);
    const WorldPolygon& reference = flip ? worldB : worldA;
    const WorldPolygon& incident = flip ? worldA : worldB;
    const int referenceEdge = flip ? sepB.edge : sepA.edge;
    const glm::vec2 n = reference.normal(referenceEdge);

    // The incident edge: the edge of the other polygon facing most against the reference normal.
    int incidentEdge = 0;
    float minDot = kHuge;
    for (int i = 0; i < incident.count; ++i) {
        const float d = glm::dot(n, incident.normal(i));
        if (d < minDot) {
            minDot = d;
            incidentEdge = i;
        }
    }

    const auto refEdgeBits = static_cast<std::uint32_t>(referenceEdge);
    const auto incEdgeBits = static_cast<std::uint32_t>(incidentEdge) << 8U;
    const std::uint32_t flipBit = flip ? 1U << 24U : 0U;
    const std::array<ClipVertex, 2> segment = {
        ClipVertex{.point = incident.vertex(incidentEdge),
                   .id = kTagPolygon | flipBit | refEdgeBits | incEdgeBits},
        ClipVertex{.point = incident.vertex(incidentEdge + 1),
                   .id = kTagPolygon | flipBit | refEdgeBits | incEdgeBits | (1U << 16U)}};

    // Clip it to the side planes of the reference edge.
    const glm::vec2 v1 = reference.vertex(referenceEdge);
    const glm::vec2 v2 = reference.vertex(referenceEdge + 1);
    const glm::vec2 tangent = glm::normalize(v2 - v1);
    std::array<ClipVertex, 2> clipped1{};
    std::array<ClipVertex, 2> clipped2{};
    if (clipSegment(segment, clipped1, -tangent, -glm::dot(tangent, v1)) < 2 ||
        clipSegment(clipped1, clipped2, tangent, glm::dot(tangent, v2)) < 2) {
        return manifold; // only possible for speculative contacts past a corner
    }

    for (const ClipVertex& vertex : clipped2) {
        const float separation = glm::dot(n, vertex.point - v1);
        if (separation <= margin) {
            ManifoldPoint& point = manifold.points[static_cast<std::size_t>(manifold.count++)];
            point.point = vertex.point - (0.5F * separation * n); // halfway to the reference face
            point.separation = separation;
            point.id = vertex.id;
        }
    }
    manifold.normal = flip ? -n : n;
    return manifold;
}

Manifold collidePolygonCircle(const Polygon& polygon, const Pose& polygonPose, const Circle& circle,
                              const Pose& circlePose, float margin, unsigned edgeMask)
{
    Manifold manifold;
    // Work in the polygon's coordinates.
    const glm::vec2 c = polygonPose.applyInverse(circlePose.apply(circle.center));
    const float r = circle.radius;

    float realSeparation = -kHuge;
    int edge = -1;
    float separation = -kHuge;
    for (int i = 0; i < polygon.count; ++i) {
        const auto index = static_cast<std::size_t>(i);
        const float s = glm::dot(polygon.normals[index], c - polygon.vertices[index]);
        realSeparation = std::max(realSeparation, s);
        if (edgeEnabled(edgeMask, i) && s > separation) {
            separation = s;
            edge = i;
        }
    }
    if (realSeparation - r > margin || edge < 0) {
        return manifold;
    }

    const auto setResult = [&](glm::vec2 normal, glm::vec2 point, float s, std::uint32_t feature) {
        manifold.normal = polygonPose.rotation.apply(normal);
        manifold.points[0] = {
            .point = polygonPose.apply(point), .separation = s, .id = kTagPolygonCircle | feature};
        manifold.count = 1;
    };
    const auto faceResult = [&] {
        const glm::vec2 n = polygon.normals[static_cast<std::size_t>(edge)];
        if (separation - r <= margin) {
            setResult(n, c - (0.5F * (separation + r) * n), separation - r,
                      static_cast<std::uint32_t>(edge));
        }
    };
    const auto vertexResult = [&](glm::vec2 v, std::uint32_t feature) {
        const glm::vec2 d = c - v;
        const float distance = glm::length(d);
        const float s = distance - r;
        if (s > margin || distance < 1e-9F) {
            return;
        }
        const glm::vec2 n = d / distance;
        setResult(n, 0.5F * (v + c - (r * n)), s, feature);
    };

    if (realSeparation < 1e-7F) {
        faceResult(); // centre inside the polygon
        return manifold;
    }
    const int next = (edge + 1) % polygon.count;
    const int previous = (edge + polygon.count - 1) % polygon.count;
    const glm::vec2 v1 = polygon.vertices[static_cast<std::size_t>(edge)];
    const glm::vec2 v2 = polygon.vertices[static_cast<std::size_t>(next)];
    if (glm::dot(c - v1, v2 - v1) < 0.0F && edgeEnabled(edgeMask, previous)) {
        vertexResult(v1, 0x100U | static_cast<std::uint32_t>(edge));
    } else if (glm::dot(c - v2, v1 - v2) < 0.0F && edgeEnabled(edgeMask, next)) {
        vertexResult(v2, 0x100U | static_cast<std::uint32_t>(next));
    } else {
        // Face region, or a corner whose other edge is disabled (shared with a neighbour).
        faceResult();
    }
    return manifold;
}

std::optional<RayCastHit> rayCast(const RayCastInput& ray, const Circle& circle, const Pose& pose)
{
    const glm::vec2 center = pose.apply(circle.center);
    const glm::vec2 s = ray.origin - center;
    const float length = glm::length(ray.translation);
    if (length <= 0.0F) {
        return std::nullopt;
    }
    const glm::vec2 direction = ray.translation / length;
    // Closest approach of the line to the centre.
    const float t = -glm::dot(s, direction);
    const glm::vec2 closest = s + (t * direction);
    const float cc = glm::dot(closest, closest);
    const float rr = circle.radius * circle.radius;
    if (cc > rr) {
        return std::nullopt;
    }
    const float distance = t - std::sqrt(rr - cc);
    if (distance < 0.0F || distance > length) {
        return std::nullopt;
    }
    const glm::vec2 point = ray.origin + (distance * direction);
    return RayCastHit{
        .point = point, .normal = glm::normalize(point - center), .fraction = distance / length};
}

std::optional<RayCastHit> rayCast(const RayCastInput& ray, const Polygon& polygon, const Pose& pose)
{
    // Clip the ray against the polygon's half planes, in the polygon's coordinates.
    const glm::vec2 p = pose.applyInverse(ray.origin);
    const glm::vec2 d = pose.rotation.applyInverse(ray.translation);
    float lower = 0.0F;
    float upper = 1.0F;
    int index = -1;
    for (int i = 0; i < polygon.count; ++i) {
        const auto k = static_cast<std::size_t>(i);
        const float numerator = glm::dot(polygon.normals[k], polygon.vertices[k] - p);
        const float denominator = glm::dot(polygon.normals[k], d);
        if (denominator == 0.0F) {
            if (numerator < 0.0F) {
                return std::nullopt; // parallel and outside this edge
            }
        } else if (denominator < 0.0F && numerator < lower * denominator) {
            lower = numerator / denominator; // entering
            index = i;
        } else if (denominator > 0.0F && numerator < upper * denominator) {
            upper = numerator / denominator; // leaving
        }
        if (upper < lower) {
            return std::nullopt;
        }
    }
    if (index < 0) {
        return std::nullopt; // starts inside
    }
    return RayCastHit{.point = ray.origin + (lower * ray.translation),
                      .normal =
                          pose.rotation.apply(polygon.normals[static_cast<std::size_t>(index)]),
                      .fraction = lower};
}

} // namespace astra2d
