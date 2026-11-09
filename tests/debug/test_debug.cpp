// Debug tools: the physics debug view (which lines for which shapes, contacts and velocities) and
// FrameStats. Both are checked without OpenGL; drawing the lines is one Renderer2D::drawLine each.

#include <astra2d/debug/frame_stats.hpp>
#include <astra2d/debug/physics_debug_draw.hpp>
#include <astra2d/physics/geometry.hpp>
#include <astra2d/physics/world.hpp>

#include <glm/geometric.hpp>
#include <glm/vector_relational.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using astra2d::BodyType;
using astra2d::DebugLine;
using astra2d::PhysicsDebugSettings;
using astra2d::PhysicsWorld;
using astra2d::ShapeDef;

namespace {

// Only the outlines, for counting.
PhysicsDebugSettings shapesOnly()
{
    return {.shapes = true, .boundingBoxes = false, .contacts = false, .velocities = false};
}

std::size_t countColor(const std::vector<DebugLine>& lines, glm::vec4 color)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(lines, [&](const DebugLine& l) { return l.color == color; }));
}

bool hasPoint(const std::vector<DebugLine>& lines, glm::vec2 point)
{
    return std::ranges::any_of(lines, [&](const DebugLine& l) {
        return glm::all(glm::lessThan(glm::abs(l.from - point), glm::vec2(1e-4F)));
    });
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Physics debug view

TEST(PhysicsDebugDrawTests, BoxOutlineHasFourEdgesAtItsCorners)
{
    PhysicsWorld world;
    const auto ground = world.createBody({.type = BodyType::Static, .position = {2.0F, 1.0F}});
    world.addShape(ground, ShapeDef::makeBox({1.0F, 0.5F}));
    const auto lines = astra2d::collectPhysicsDebug(world, shapesOnly());
    ASSERT_EQ(lines.size(), 4U);
    EXPECT_EQ(countColor(lines, astra2d::PhysicsDebugColors{}.staticBody), 4U);
    for (const glm::vec2 corner : {glm::vec2(1.0F, 0.5F), glm::vec2(3.0F, 0.5F),
                                   glm::vec2(3.0F, 1.5F), glm::vec2(1.0F, 1.5F)}) {
        EXPECT_TRUE(hasPoint(lines, corner)) << corner.x << ", " << corner.y;
    }
}

TEST(PhysicsDebugDrawTests, OutlinesFollowTheBodyRotation)
{
    PhysicsWorld world({.gravity = {0.0F, 0.0F}});
    const auto box = world.createBody({.angle = std::numbers::pi_v<float> / 2.0F});
    world.addShape(box, ShapeDef::makeBox({2.0F, 0.5F})); // wide, turned upright
    const auto lines = astra2d::collectPhysicsDebug(world, shapesOnly());
    for (const DebugLine& l : lines) {
        EXPECT_NEAR(std::abs(l.from.x), 0.5F, 1e-4F);
        EXPECT_NEAR(std::abs(l.from.y), 2.0F, 1e-4F);
    }
}

TEST(PhysicsDebugDrawTests, CircleIsSegmentsPlusASpoke)
{
    PhysicsWorld world({.gravity = {0.0F, 0.0F}});
    const auto ball = world.createBody({.position = {1.0F, 1.0F}});
    world.addShape(ball, ShapeDef::makeCircle(0.5F));
    PhysicsDebugSettings settings = shapesOnly();
    settings.circleSegments = 12;
    const auto lines = astra2d::collectPhysicsDebug(world, settings);
    ASSERT_EQ(lines.size(), 13U);
    for (const DebugLine& l : lines) {
        EXPECT_NEAR(glm::length(l.to - glm::vec2(1.0F)), 0.5F, 1e-4F); // every end on the rim
    }
    EXPECT_EQ(countColor(lines, astra2d::PhysicsDebugColors{}.awakeBody), 13U);
}

TEST(PhysicsDebugDrawTests, ColoursShowBodyTypeAndShapeKind)
{
    PhysicsWorld world({.gravity = {0.0F, 0.0F}});
    const astra2d::PhysicsDebugColors colors;
    const auto mover = world.createBody({.type = BodyType::Kinematic});
    world.addShape(mover, ShapeDef::makeBox({0.5F, 0.5F}));
    const auto zone = world.createBody({.type = BodyType::Static, .position = {5.0F, 0.0F}});
    ShapeDef trigger = ShapeDef::makeBox({0.5F, 0.5F});
    trigger.isTrigger = true;
    world.addShape(zone, trigger);
    const auto ledge = world.createBody({.type = BodyType::Static, .position = {9.0F, 0.0F}});
    ShapeDef plank = ShapeDef::makeBox({0.5F, 0.1F});
    plank.oneWay = true;
    world.addShape(ledge, plank);
    const auto sleeper = world.createBody({.position = {-5.0F, 0.0F}});
    world.addShape(sleeper, ShapeDef::makeBox({0.5F, 0.5F}));
    world.setAwake(sleeper, false); // after addShape(), which wakes the body

    const auto lines = astra2d::collectPhysicsDebug(world, shapesOnly());
    EXPECT_EQ(countColor(lines, colors.kinematicBody), 4U);
    EXPECT_EQ(countColor(lines, colors.trigger), 4U);
    EXPECT_EQ(countColor(lines, colors.oneWay), 1U); // only its top collides; the rest is faint
    EXPECT_EQ(countColor(lines, colors.sleepingBody), 4U);
}

TEST(PhysicsDebugDrawTests, EdgesThatCannotCollideAreFaint)
{
    PhysicsWorld world;
    const auto tile = world.createBody({.type = BodyType::Static});
    ShapeDef top = ShapeDef::makeBox({0.5F, 0.5F});
    top.edgeMask = static_cast<std::uint8_t>(astra2d::kBoxTop); // as for a tile inside a floor
    world.addShape(tile, top);
    const astra2d::PhysicsDebugColors colors;
    glm::vec4 faint = colors.staticBody;
    faint.a *= colors.disabledEdgeAlpha;
    const auto lines = astra2d::collectPhysicsDebug(world, shapesOnly());
    EXPECT_EQ(countColor(lines, colors.staticBody), 1U);
    EXPECT_EQ(countColor(lines, faint), 3U);
}

TEST(PhysicsDebugDrawTests, BoundingBoxesContactsAndVelocities)
{
    PhysicsWorld world;
    const astra2d::PhysicsDebugColors colors;
    const auto ground = world.createBody({.type = BodyType::Static, .position = {0.0F, -0.5F}});
    world.addShape(ground, ShapeDef::makeBox({5.0F, 0.5F}));
    const auto box = world.createBody({.position = {0.0F, 0.5F}, .allowSleep = false});
    world.addShape(box, ShapeDef::makeBox({0.5F, 0.5F}));
    for (int i = 0; i < 30; ++i) {
        world.step(1.0F / 60.0F); // resting: two contact points
    }

    PhysicsDebugSettings contactsOnly{.shapes = false, .contacts = true, .velocities = false};
    const auto contacts = astra2d::collectPhysicsDebug(world, contactsOnly);
    EXPECT_EQ(contacts.size(), 2U * 3U); // a cross and a normal per point
    EXPECT_EQ(countColor(contacts, colors.contact), contacts.size());

    const PhysicsDebugSettings boxesOnly{
        .shapes = false, .boundingBoxes = true, .contacts = false, .velocities = false};
    EXPECT_EQ(astra2d::collectPhysicsDebug(world, boxesOnly).size(), 2U * 4U);

    const PhysicsDebugSettings velocityOnly{.shapes = false, .contacts = false, .velocities = true};
    EXPECT_TRUE(astra2d::collectPhysicsDebug(world, velocityOnly).empty()); // at rest
    world.setVelocity(box, {3.0F, 0.0F});
    const auto arrow = astra2d::collectPhysicsDebug(world, velocityOnly);
    ASSERT_EQ(arrow.size(), 3U); // shaft and two head strokes
    EXPECT_NEAR(arrow[0].to.x - arrow[0].from.x, 3.0F * velocityOnly.velocityScale, 1e-4F);
    EXPECT_EQ(countColor(arrow, colors.velocity), 3U);
}

TEST(PhysicsDebugDrawTests, EmptyWorldDrawsNothing)
{
    const PhysicsWorld world;
    EXPECT_TRUE(astra2d::collectPhysicsDebug(world).empty());
}

// ---------------------------------------------------------------------------------------------
// FrameStats

TEST(FrameStatsTests, ReportsOncePerInterval)
{
    astra2d::FrameStats stats(0.5F);
    EXPECT_EQ(stats.report().frames, 0);
    EXPECT_FALSE(stats.addFrame(0.125F, {.drawCalls = 2, .quads = 100}));
    EXPECT_FALSE(stats.addFrame(0.25F, {.drawCalls = 4, .quads = 300}));
    EXPECT_TRUE(stats.addFrame(0.125F, {.drawCalls = 3, .quads = 200})); // 0.5 s
    const astra2d::FrameReport& r = stats.report();
    EXPECT_EQ(r.frames, 3);
    EXPECT_FLOAT_EQ(r.fps, 6.0F);
    EXPECT_NEAR(r.averageMs, 500.0F / 3.0F, 1e-3F);
    EXPECT_FLOAT_EQ(r.minMs, 125.0F);
    EXPECT_FLOAT_EQ(r.maxMs, 250.0F);
    EXPECT_FLOAT_EQ(r.drawCalls, 3.0F);
    EXPECT_FLOAT_EQ(r.quads, 200.0F);

    // The next interval starts from zero; the report stays until it ends.
    EXPECT_FALSE(stats.addFrame(0.125F));
    EXPECT_EQ(stats.report().frames, 3);
    EXPECT_TRUE(stats.addFrame(0.5F));
    EXPECT_EQ(stats.report().frames, 2);
    EXPECT_FLOAT_EQ(stats.report().minMs, 125.0F);
    EXPECT_FLOAT_EQ(stats.report().drawCalls, 0.0F);
}

TEST(FrameStatsTests, SummaryNamesTheNumbers)
{
    astra2d::FrameStats stats(0.25F);
    stats.addFrame(0.25F, {.drawCalls = 3, .quads = 512});
    const std::string summary = stats.summary();
    EXPECT_NE(summary.find("4.0 FPS"), std::string::npos) << summary;
    EXPECT_NE(summary.find("frame 250.00 ms"), std::string::npos) << summary;
    EXPECT_NE(summary.find("3.0 draw calls"), std::string::npos) << summary;
    EXPECT_NE(summary.find("512 quads"), std::string::npos) << summary;
}

TEST(FrameStatsTests, RestartDropsTheIntervalInProgress)
{
    astra2d::FrameStats stats(1.0F);
    stats.addFrame(0.75F); // e.g. a long frame before a pause
    stats.restart();
    EXPECT_FALSE(stats.addFrame(0.5F));
    EXPECT_TRUE(stats.addFrame(0.5F));
    EXPECT_FLOAT_EQ(stats.report().maxMs, 500.0F);
}

TEST(FrameStatsTests, RejectsABadInterval)
{
    EXPECT_THROW(astra2d::FrameStats(0.0F), std::invalid_argument);
    EXPECT_THROW(astra2d::FrameStats(-1.0F), std::invalid_argument);
}
