// Narrow phase (every shape pair, edge masks, speculative points), geometry helpers, ray casts and
// the three broad phases.

#include <astra2d/physics/broad_phase.hpp>
#include <astra2d/physics/collision.hpp>
#include <astra2d/physics/geometry.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

using astra2d::Aabb;
using astra2d::Circle;
using astra2d::Manifold;
using astra2d::Polygon;
using astra2d::Pose;
using astra2d::Rotation;

namespace {

constexpr float kEpsilon = 1e-5F;

void expectNear(glm::vec2 actual, glm::vec2 expected, float tolerance = kEpsilon)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
}

Pose at(glm::vec2 position, float angle = 0.0F)
{
    return {.position = position, .rotation = Rotation::fromAngle(angle)};
}

Aabb box(glm::vec2 center, glm::vec2 halfExtents)
{
    return {.min = center - halfExtents, .max = center + halfExtents};
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Geometry

TEST(GeometryTests, MakeBoxStartsAtTheBottomEdge)
{
    const Polygon b = astra2d::makeBox({2.0F, 1.0F}, {1.0F, 0.0F});
    ASSERT_EQ(b.count, 4);
    expectNear(b.vertices[0], {-1.0F, -1.0F});
    expectNear(b.vertices[2], {3.0F, 1.0F});
    expectNear(b.normals[0], {0.0F, -1.0F}); // kBoxBottom
    expectNear(b.normals[1], {1.0F, 0.0F});  // kBoxRight
    expectNear(b.normals[2], {0.0F, 1.0F});  // kBoxTop
    expectNear(b.normals[3], {-1.0F, 0.0F}); // kBoxLeft
    expectNear(b.centroid, {1.0F, 0.0F});
}

TEST(GeometryTests, MakePolygonBuildsTheConvexHull)
{
    // A square given out of order, with an interior point, a duplicate and a point on an edge.
    const std::vector<glm::vec2> points = {{1, 1},  {-1, -1}, {0, 0}, {1, -1},
                                           {-1, 1}, {1, 1},   {0, -1}};
    const Polygon p = astra2d::makePolygon(points);
    ASSERT_EQ(p.count, 4);
    for (int i = 0; i < p.count; ++i) {
        const auto k = static_cast<std::size_t>(i);
        const glm::vec2 edge = p.vertices[(k + 1) % 4] - p.vertices[k];
        // Counter-clockwise: the outward normal is the edge turned clockwise.
        EXPECT_GT(astra2d::cross(edge, p.vertices[(k + 2) % 4] - p.vertices[k]), 0.0F);
        EXPECT_NEAR(glm::dot(p.normals[k], edge), 0.0F, kEpsilon);
    }
    expectNear(p.centroid, {0.0F, 0.0F});
}

TEST(GeometryTests, MakePolygonRejectsDegenerateInput)
{
    const std::vector<glm::vec2> line = {{0, 0}, {1, 1}, {2, 2}};
    EXPECT_THROW((void)astra2d::makePolygon(line), std::invalid_argument);
    const std::vector<glm::vec2> two = {{0, 0}, {1, 0}};
    EXPECT_THROW((void)astra2d::makePolygon(two), std::invalid_argument);
    std::vector<glm::vec2> circle;
    for (int i = 0; i < 12; ++i) {
        const float angle = static_cast<float>(i) * 2.0F * std::numbers::pi_v<float> / 12.0F;
        circle.emplace_back(std::cos(angle), std::sin(angle));
    }
    EXPECT_THROW((void)astra2d::makePolygon(circle), std::invalid_argument); // > 8 corners
}

TEST(GeometryTests, MassOfBoxAndCircle)
{
    const auto boxMass = astra2d::computeMass(astra2d::makeBox({2.0F, 1.0F}, {5.0F, 0.0F}));
    EXPECT_NEAR(boxMass.area, 8.0F, 1e-4F);
    expectNear(boxMass.center, {5.0F, 0.0F}, 1e-4F);
    // A w x h box: I = area * (w² + h²) / 12 about its centre.
    EXPECT_NEAR(boxMass.inertia, 8.0F * (16.0F + 4.0F) / 12.0F, 1e-3F);

    const auto circleMass = astra2d::computeMass(Circle{.center = {1.0F, 2.0F}, .radius = 2.0F});
    EXPECT_NEAR(circleMass.area, std::numbers::pi_v<float> * 4.0F, 1e-4F);
    EXPECT_NEAR(circleMass.inertia, 0.5F * circleMass.area * 4.0F, 1e-3F);
}

TEST(GeometryTests, AabbOfRotatedBox)
{
    const Polygon square = astra2d::makeBox({1.0F, 1.0F});
    const Aabb bounds =
        astra2d::computeAabb(square, at({3.0F, 0.0F}, std::numbers::pi_v<float> / 4.0F));
    const float half = std::numbers::sqrt2_v<float>;
    expectNear(bounds.min, {3.0F - half, -half}, 1e-4F);
    expectNear(bounds.max, {3.0F + half, half}, 1e-4F);
}

// ---------------------------------------------------------------------------------------------
// AABB vs AABB

TEST(CollideAabbsTests, NormalIsTheAxisOfLeastPenetration)
{
    // B overlaps A by 0.2 along x and 1 along y: pushed out along +x.
    const Manifold m = astra2d::collideAabbs(box({0, 0}, {1, 1}), box({1.8F, 0.5F}, {1, 1}));
    ASSERT_EQ(m.count, 2);
    expectNear(m.normal, {1.0F, 0.0F});
    EXPECT_NEAR(m.points[0].separation, -0.2F, kEpsilon);
    // The points span the overlap along the face, halfway between the faces.
    expectNear(m.points[0].point, {0.9F, -0.5F});
    expectNear(m.points[1].point, {0.9F, 1.0F});
    EXPECT_NE(m.points[0].id, m.points[1].id);
}

TEST(CollideAabbsTests, NormalPointsFromAToB)
{
    const Manifold below = astra2d::collideAabbs(box({0, 0}, {1, 1}), box({0, -1.9F}, {1, 1}));
    ASSERT_GT(below.count, 0);
    expectNear(below.normal, {0.0F, -1.0F});
    const Manifold left = astra2d::collideAabbs(box({0, 0}, {1, 1}), box({-1.5F, 0}, {1, 1}));
    expectNear(left.normal, {-1.0F, 0.0F});
}

TEST(CollideAabbsTests, SeparatedBoxesDoNotCollideButTouchingOnesDo)
{
    EXPECT_EQ(astra2d::collideAabbs(box({0, 0}, {1, 1}), box({2.1F, 0}, {1, 1})).count, 0);
    // Diagonal neighbours overlapping on one axis only.
    EXPECT_EQ(astra2d::collideAabbs(box({0, 0}, {1, 1}), box({2.1F, 1.5F}, {1, 1})).count, 0);
    const Manifold touching = astra2d::collideAabbs(box({0, 0}, {1, 1}), box({0, 2}, {1, 1}));
    ASSERT_EQ(touching.count, 2);
    EXPECT_NEAR(touching.points[0].separation, 0.0F, kEpsilon);
}

TEST(CollideAabbsTests, MarginGivesSpeculativePoints)
{
    const Manifold m = astra2d::collideAabbs(box({0, 0}, {1, 1}), box({0, 2.3F}, {1, 1}), 0.5F);
    ASSERT_EQ(m.count, 2);
    EXPECT_NEAR(m.points[0].separation, 0.3F, kEpsilon);
    expectNear(m.normal, {0.0F, 1.0F});
}

TEST(CollideAabbsTests, DisabledEdgesNeverGiveTheNormal)
{
    // A box sliding over a seam: it enters the next tile 0.01 sideways, 0.1 from above. Without
    // masks the normal would be -x (a "ghost" wall). With the tile's left edge disabled (a solid
    // neighbour) it is the top edge.
    const Aabb tile = box({0, 0}, {0.5F, 0.5F});
    const Aabb player = box({-0.99F, 0.9F}, {0.5F, 0.5F});
    EXPECT_EQ(astra2d::collideAabbs(tile, player).normal, glm::vec2(-1.0F, 0.0F));
    const unsigned noLeft = 0x0FU & ~unsigned{astra2d::kBoxLeft};
    const Manifold masked = astra2d::collideAabbs(tile, player, 0.0F, noLeft);
    ASSERT_GT(masked.count, 0);
    expectNear(masked.normal, {0.0F, 1.0F});
    EXPECT_NEAR(masked.points[0].separation, -0.1F, kEpsilon);
    // Masks do not invent contacts: separated boxes still do not touch.
    EXPECT_EQ(astra2d::collideAabbs(tile, box({0, 2}, {0.5F, 0.5F}), 0.0F, noLeft).count, 0);
    // Every edge disabled: no contact.
    EXPECT_EQ(astra2d::collideAabbs(tile, player, 0.0F, 0U).count, 0);
}

// ---------------------------------------------------------------------------------------------
// Circle vs circle

TEST(CollideCirclesTests, OverlapGivesOnePointBetweenTheSurfaces)
{
    const Manifold m = astra2d::collideCircles({.center = {0, 0}, .radius = 1},
                                               {.center = {1.5F, 0}, .radius = 1});
    ASSERT_EQ(m.count, 1);
    expectNear(m.normal, {1.0F, 0.0F});
    EXPECT_NEAR(m.points[0].separation, -0.5F, kEpsilon);
    expectNear(m.points[0].point, {0.75F, 0.0F});
    EXPECT_EQ(
        astra2d::collideCircles({.center = {0, 0}, .radius = 1}, {.center = {2.1F, 0}, .radius = 1})
            .count,
        0);
}

TEST(CollideCirclesTests, ConcentricCirclesStillGetANormal)
{
    const Manifold m = astra2d::collideCircles({.center = {1, 1}, .radius = 1},
                                               {.center = {1, 1}, .radius = 0.5F});
    ASSERT_EQ(m.count, 1);
    EXPECT_NEAR(glm::length(m.normal), 1.0F, kEpsilon);
    EXPECT_NEAR(m.points[0].separation, -1.5F, kEpsilon);
}

// ---------------------------------------------------------------------------------------------
// AABB vs circle

TEST(CollideAabbCircleTests, FaceCornerAndInside)
{
    const Aabb b = box({0, 0}, {1, 1});
    const Manifold face = astra2d::collideAabbCircle(b, {.center = {0.3F, 1.4F}, .radius = 0.5F});
    ASSERT_EQ(face.count, 1);
    expectNear(face.normal, {0.0F, 1.0F});
    EXPECT_NEAR(face.points[0].separation, -0.1F, kEpsilon);

    const Manifold corner = astra2d::collideAabbCircle(b, {.center = {1.3F, 1.3F}, .radius = 0.5F});
    ASSERT_EQ(corner.count, 1);
    const float diagonal = std::numbers::sqrt2_v<float> / 2.0F;
    expectNear(corner.normal, {diagonal, diagonal});
    EXPECT_NEAR(corner.points[0].separation, (0.3F * std::numbers::sqrt2_v<float>)-0.5F, kEpsilon);

    EXPECT_EQ(astra2d::collideAabbCircle(b, {.center = {1.4F, 1.4F}, .radius = 0.5F}).count, 0);

    // Centre inside, nearest to the right face.
    const Manifold inside = astra2d::collideAabbCircle(b, {.center = {0.8F, 0.1F}, .radius = 0.5F});
    ASSERT_EQ(inside.count, 1);
    expectNear(inside.normal, {1.0F, 0.0F});
    EXPECT_NEAR(inside.points[0].separation, -0.2F - 0.5F, kEpsilon);
}

TEST(CollideAabbCircleTests, CornerOfADisabledEdgeActsAsTheOtherFace)
{
    // A ball rolling over the top-right corner where a neighbour tile continues to the right:
    // the corner must act as part of the flat top.
    const Aabb tile = box({0, 0}, {0.5F, 0.5F});
    const unsigned noRight = 0x0FU & ~unsigned{astra2d::kBoxRight};
    const Manifold m =
        astra2d::collideAabbCircle(tile, {.center = {0.6F, 0.9F}, .radius = 0.5F}, 0.0F, noRight);
    ASSERT_EQ(m.count, 1);
    expectNear(m.normal, {0.0F, 1.0F});
    EXPECT_NEAR(m.points[0].separation, -0.1F, kEpsilon);
}

// ---------------------------------------------------------------------------------------------
// Polygons

TEST(CollidePolygonsTests, MatchesTheAabbPathForAlignedBoxes)
{
    const Polygon a = astra2d::makeBox({1.0F, 1.0F});
    const Polygon b = astra2d::makeBox({0.5F, 0.5F});
    for (const glm::vec2 offset :
         {glm::vec2(1.4F, 0.2F), glm::vec2(-0.3F, -1.3F), glm::vec2(0.2F, 1.45F)}) {
        const Manifold polygon = astra2d::collidePolygons(a, at({0, 0}), b, at(offset));
        const Manifold aabb = astra2d::collideAabbs(box({0, 0}, {1, 1}), box(offset, {0.5F, 0.5F}));
        ASSERT_EQ(polygon.count, aabb.count);
        expectNear(polygon.normal, aabb.normal);
        EXPECT_NEAR(polygon.minSeparation(), aabb.minSeparation(), kEpsilon);
    }
}

TEST(CollidePolygonsTests, RotatedBoxOnGroundTouchesWithOneCorner)
{
    const Polygon ground = astra2d::makeBox({5.0F, 0.5F});
    const Polygon square = astra2d::makeBox({0.5F, 0.5F});
    // Rotated 45°: the lowest corner is sqrt(2)/2 below the centre; 0.05 into the ground.
    const float half = std::numbers::sqrt2_v<float> / 2.0F;
    const Manifold m =
        astra2d::collidePolygons(ground, at({0, 0}), square,
                                 at({0.0F, 0.5F + half - 0.05F}, std::numbers::pi_v<float> / 4.0F));
    ASSERT_EQ(m.count, 1);
    expectNear(m.normal, {0.0F, 1.0F});
    EXPECT_NEAR(m.points[0].separation, -0.05F, 1e-4F);
    expectNear(m.points[0].point, {0.0F, 0.475F}, 1e-4F);
}

TEST(CollidePolygonsTests, FlatBoxOnGroundTouchesAlongAnEdge)
{
    const Polygon ground = astra2d::makeBox({5.0F, 0.5F});
    const Polygon square = astra2d::makeBox({0.5F, 0.5F});
    // Slightly rotated so the polygon path (not the AABB one) is used.
    const Manifold m =
        astra2d::collidePolygons(ground, at({0, 0}), square, at({2.0F, 0.98F}, 0.001F));
    ASSERT_EQ(m.count, 2);
    expectNear(m.normal, {0.0F, 1.0F});
    EXPECT_LT(m.points[0].separation, 0.0F);
    EXPECT_NEAR(std::abs(m.points[0].point.x - m.points[1].point.x), 1.0F, 1e-2F);
}

TEST(CollidePolygonsTests, ChoosesTheSameFacesInAnyLengthUnit)
{
    // Two boxes resting on each other, slightly turned, so both faces are nearly as good a
    // reference: the tolerance that decides between them must scale with the length unit, or a
    // game measured in pixels picks other faces (and loses its warm starting) than one in metres.
    const float scale = 32.0F;
    // With these, the turned box's face is better by 0.0001 to 0.0003 m: within the tolerance
    // (0.0005 m), so the first box's face stays the reference, in metres and in pixels.
    for (const float angle : {1e-3F, 3e-3F}) {
        const glm::vec2 offset{-0.1F, 0.99F};
        const Manifold metres =
            astra2d::collidePolygons(astra2d::makeBox({0.5F, 0.5F}), at({0, 0}),
                                     astra2d::makeBox({0.5F, 0.5F}), at(offset, angle));
        const Manifold pixels =
            astra2d::collidePolygons(astra2d::makeBox(glm::vec2(0.5F * scale)), at({0, 0}),
                                     astra2d::makeBox(glm::vec2(0.5F * scale)),
                                     at(offset * scale, angle), 0.0F, 0xFFU, 0xFFU, scale);
        ASSERT_EQ(metres.count, pixels.count) << angle;
        for (int i = 0; i < metres.count; ++i) {
            const auto index = static_cast<std::size_t>(i);
            EXPECT_EQ(metres.points[index].id, pixels.points[index].id) << angle;
        }
    }
}

TEST(CollidePolygonsTests, TriangleAgainstBoxAndSeparation)
{
    const std::vector<glm::vec2> points = {{-1, 0}, {1, 0}, {0, 1}};
    const Polygon triangle = astra2d::makePolygon(points);
    const Polygon square = astra2d::makeBox({0.5F, 0.5F});
    // The square's bottom-left corner pokes into the triangle's right slope.
    const Manifold m = astra2d::collidePolygons(triangle, at({0, 0}), square, at({0.9F, 0.9F}));
    ASSERT_GE(m.count, 1);
    EXPECT_GT(m.normal.x, 0.0F);
    EXPECT_GT(m.normal.y, 0.0F);
    EXPECT_LT(m.minSeparation(), 0.0F);
    EXPECT_EQ(astra2d::collidePolygons(triangle, at({0, 0}), square, at({2.0F, 2.0F})).count, 0);
}

TEST(CollidePolygonCircleTests, MatchesTheAabbPathForAlignedBoxes)
{
    const Polygon square = astra2d::makeBox({1.0F, 1.0F});
    for (const glm::vec2 center : {glm::vec2(0.3F, 1.4F), glm::vec2(1.3F, 1.3F),
                                   glm::vec2(0.8F, 0.1F), glm::vec2(-1.2F, 0.0F)}) {
        const Circle circle{.center = center, .radius = 0.5F};
        const Manifold polygon = astra2d::collidePolygonCircle(
            square, at({0, 0}), {.center = {0, 0}, .radius = 0.5F}, at(center));
        const Manifold aabb = astra2d::collideAabbCircle(box({0, 0}, {1, 1}), circle);
        ASSERT_EQ(polygon.count, aabb.count) << center.x << ", " << center.y;
        expectNear(polygon.normal, aabb.normal);
        EXPECT_NEAR(polygon.points[0].separation, aabb.points[0].separation, kEpsilon);
        expectNear(polygon.points[0].point, aabb.points[0].point);
    }
}

TEST(CollidePolygonCircleTests, RotatedBoxCorner)
{
    // A box rotated 45° has a corner pointing up at y = sqrt(2); a ball resting on it.
    const Polygon square = astra2d::makeBox({1.0F, 1.0F});
    const float top = std::numbers::sqrt2_v<float>;
    const Manifold m =
        astra2d::collidePolygonCircle(square, at({0, 0}, std::numbers::pi_v<float> / 4.0F),
                                      {.center = {0, 0}, .radius = 0.5F}, at({0.0F, top + 0.4F}));
    ASSERT_EQ(m.count, 1);
    expectNear(m.normal, {0.0F, 1.0F}, 1e-4F);
    EXPECT_NEAR(m.points[0].separation, -0.1F, 1e-4F);
}

// ---------------------------------------------------------------------------------------------
// Ray casts

TEST(RayCastTests, CircleHitMissAndInside)
{
    const Circle circle{.center = {0, 0}, .radius = 1};
    const auto hit =
        astra2d::rayCast({.origin = {-3, 0}, .translation = {6, 0}}, circle, at({1, 0}));
    ASSERT_TRUE(hit.has_value());
    const astra2d::RayCastHit h = hit.value_or(astra2d::RayCastHit{});
    expectNear(h.point, {0, 0});
    expectNear(h.normal, {-1, 0});
    EXPECT_NEAR(h.fraction, 0.5F, kEpsilon);
    EXPECT_FALSE(astra2d::rayCast({.origin = {-3, 2}, .translation = {6, 0}}, circle, at({1, 0}))
                     .has_value());
    EXPECT_FALSE(astra2d::rayCast({.origin = {-3, 0}, .translation = {1, 0}}, circle, at({1, 0}))
                     .has_value());
    EXPECT_FALSE(astra2d::rayCast({.origin = {1, 0}, .translation = {6, 0}}, circle, at({1, 0}))
                     .has_value());
}

TEST(RayCastTests, PolygonHitMissAndInside)
{
    const Polygon square = astra2d::makeBox({1.0F, 1.0F});
    const auto hit =
        astra2d::rayCast({.origin = {0, 5}, .translation = {0, -10}}, square, at({0, 0}));
    ASSERT_TRUE(hit.has_value());
    const astra2d::RayCastHit h = hit.value_or(astra2d::RayCastHit{});
    expectNear(h.point, {0, 1});
    expectNear(h.normal, {0, 1});
    EXPECT_NEAR(h.fraction, 0.4F, kEpsilon);
    // Rotated 45°: the top corner is at sqrt(2).
    const auto rotated = astra2d::rayCast({.origin = {0, 5}, .translation = {0, -10}}, square,
                                          at({0, 0}, std::numbers::pi_v<float> / 4.0F));
    ASSERT_TRUE(rotated.has_value());
    EXPECT_NEAR(rotated.value_or(astra2d::RayCastHit{}).point.y, std::numbers::sqrt2_v<float>,
                1e-4F);
    EXPECT_FALSE(astra2d::rayCast({.origin = {3, 5}, .translation = {0, -10}}, square, at({0, 0}))
                     .has_value());
    EXPECT_FALSE(astra2d::rayCast({.origin = {0, 0}, .translation = {0, -10}}, square, at({0, 0}))
                     .has_value());
}

// ---------------------------------------------------------------------------------------------
// Broad phase

namespace {

std::vector<astra2d::BroadPhaseProxy> randomProxies(int count, unsigned seed)
{
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> position(0.0F, 50.0F);
    std::uniform_real_distribution<float> size(0.2F, 1.5F);
    std::vector<astra2d::BroadPhaseProxy> proxies;
    for (int i = 0; i < count; ++i) {
        const glm::vec2 p(position(random), position(random));
        const glm::vec2 h(size(random), size(random));
        proxies.push_back({.aabb = {.min = p - h, .max = p + h},
                           .id = static_cast<std::uint32_t>((i * 3) + 7),
                           .movable = i % 5 != 0});
    }
    // A long static ground covering many cells, and a large moving box.
    proxies.push_back(
        {.aabb = {.min = {-10.0F, -1.0F}, .max = {60.0F, 0.5F}}, .id = 1, .movable = false});
    proxies.push_back(
        {.aabb = {.min = {10.0F, 10.0F}, .max = {40.0F, 40.0F}}, .id = 2, .movable = true});
    return proxies;
}

std::vector<astra2d::ProxyPair> pairsOf(astra2d::BroadPhaseType type,
                                        const std::vector<astra2d::BroadPhaseProxy>& proxies,
                                        std::int64_t* tests = nullptr, float cellSize = 0.0F)
{
    astra2d::BroadPhase broadPhase(type, cellSize);
    std::vector<astra2d::ProxyPair> pairs;
    broadPhase.findPairs(proxies, pairs);
    if (tests != nullptr) {
        *tests = broadPhase.pairTests();
    }
    std::ranges::sort(pairs);
    return pairs;
}

} // namespace

TEST(BroadPhaseTests, AllMethodsFindTheSamePairs)
{
    const auto proxies = randomProxies(600, 1234);
    std::int64_t bruteTests = 0;
    std::int64_t sweepTests = 0;
    std::int64_t gridTests = 0;
    const auto brute = pairsOf(astra2d::BroadPhaseType::BruteForce, proxies, &bruteTests);
    const auto sweep = pairsOf(astra2d::BroadPhaseType::SortAndSweep, proxies, &sweepTests);
    const auto grid = pairsOf(astra2d::BroadPhaseType::UniformGrid, proxies, &gridTests);
    ASSERT_FALSE(brute.empty());
    EXPECT_EQ(sweep, brute);
    EXPECT_EQ(grid, brute);
    EXPECT_EQ(pairsOf(astra2d::BroadPhaseType::UniformGrid, proxies, nullptr, 0.7F), brute);
    EXPECT_EQ(pairsOf(astra2d::BroadPhaseType::UniformGrid, proxies, nullptr, 25.0F), brute);
    // No pair twice.
    EXPECT_EQ(std::ranges::adjacent_find(grid), grid.end());
    // The point of a broad phase: far fewer box tests than all pairs.
    EXPECT_LT(sweepTests, bruteTests / 5);
    EXPECT_LT(gridTests, bruteTests / 5);
}

TEST(BroadPhaseTests, PairsOfTwoUnmovableProxiesAreSkipped)
{
    const std::vector<astra2d::BroadPhaseProxy> proxies = {
        {.aabb = {.min = {0, 0}, .max = {2, 2}}, .id = 0, .movable = false},
        {.aabb = {.min = {1, 1}, .max = {3, 3}}, .id = 1, .movable = false},
        {.aabb = {.min = {1, 0}, .max = {2, 1}}, .id = 2, .movable = true},
    };
    const std::vector<astra2d::ProxyPair> expected = {{0, 2}, {1, 2}};
    for (const auto type :
         {astra2d::BroadPhaseType::BruteForce, astra2d::BroadPhaseType::SortAndSweep,
          astra2d::BroadPhaseType::UniformGrid}) {
        EXPECT_EQ(pairsOf(type, proxies), expected);
    }
}
