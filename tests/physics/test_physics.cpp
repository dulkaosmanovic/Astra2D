// PhysicsWorld: body types, gravity, resolution with restitution and friction, stacking,
// determinism, events, triggers, one-way platforms, sleeping, bullets, queries and tile maps.

#include <astra2d/physics/tilemap.hpp>
#include <astra2d/physics/world.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

using astra2d::BodyDef;
using astra2d::BodyId;
using astra2d::BodyType;
using astra2d::PhysicsWorld;
using astra2d::ShapeDef;
using astra2d::ShapeId;

namespace {

constexpr float kDt = 1.0F / 60.0F;

void run(PhysicsWorld& world, float seconds)
{
    const int steps = static_cast<int>(std::lround(seconds / kDt));
    for (int i = 0; i < steps; ++i) {
        world.step(kDt);
    }
}

// A static ground whose top is at y = 0.
BodyId addGround(PhysicsWorld& world, float friction = 0.5F, float halfWidth = 50.0F)
{
    const BodyId ground = world.createBody({.type = BodyType::Static, .position = {0.0F, -0.5F}});
    ShapeDef def = ShapeDef::makeBox({halfWidth, 0.5F});
    def.friction = friction;
    world.addShape(ground, def);
    return ground;
}

BodyId addBox(PhysicsWorld& world, glm::vec2 position, glm::vec2 halfExtents = glm::vec2(0.5F),
              BodyDef def = {}, float friction = 0.5F, float restitution = 0.0F)
{
    def.position = position;
    const BodyId body = world.createBody(def);
    ShapeDef shape = ShapeDef::makeBox(halfExtents);
    shape.friction = friction;
    shape.restitution = restitution;
    world.addShape(body, shape);
    return body;
}

BodyId addBall(PhysicsWorld& world, glm::vec2 position, float radius = 0.5F, BodyDef def = {},
               float restitution = 0.0F)
{
    def.position = position;
    const BodyId body = world.createBody(def);
    ShapeDef shape = ShapeDef::makeCircle(radius);
    shape.restitution = restitution;
    world.addShape(body, shape);
    return body;
}

// A pyramid-free tower of `count` unit boxes standing on the ground.
std::vector<BodyId> addStack(PhysicsWorld& world, int count)
{
    std::vector<BodyId> boxes;
    boxes.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        boxes.push_back(addBox(world, {0.0F, 0.5F + static_cast<float>(i)}));
    }
    return boxes;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Bodies

TEST(PhysicsWorldTests, HandlesBecomeInvalidAndSlotsAreReused)
{
    PhysicsWorld world;
    const BodyId a = world.createBody();
    const ShapeId shape = world.addShape(a, ShapeDef::makeCircle(1.0F));
    EXPECT_TRUE(world.valid(a));
    EXPECT_TRUE(world.valid(shape));
    EXPECT_EQ(world.body(shape), a);
    world.destroyBody(a);
    EXPECT_FALSE(world.valid(a));
    EXPECT_FALSE(world.valid(shape));
    const BodyId b = world.createBody();
    EXPECT_EQ(b.index, a.index);
    EXPECT_FALSE(world.valid(a)); // stale handle to a reused slot
    EXPECT_TRUE(world.valid(b));
    EXPECT_EQ(world.bodyCount(), 1U);
    EXPECT_TRUE(BodyId{}.isNull());
}

TEST(PhysicsWorldTests, DynamicBodiesFallStaticOnesStayAndKinematicOnesKeepTheirVelocity)
{
    PhysicsWorld world;
    const BodyId falling = addBall(world, {0.0F, 100.0F});
    const BodyId fixed = addBall(world, {5.0F, 100.0F}, 0.5F, {.type = BodyType::Static});
    const BodyId kinematic = addBall(world, {10.0F, 100.0F}, 0.5F,
                                     {.type = BodyType::Kinematic, .velocity = {1.0F, 0.0F}});
    const BodyId floating = addBall(world, {15.0F, 100.0F}, 0.5F, {.gravityScale = 0.0F});
    run(world, 1.0F);
    // Semi-implicit Euler: the velocity is exact, the position slightly ahead.
    EXPECT_NEAR(world.velocity(falling).y, -9.81F, 1e-3F);
    EXPECT_NEAR(world.position(falling).y, 100.0F - (0.5F * 9.81F) - (0.5F * 9.81F * kDt), 1e-2F);
    EXPECT_EQ(world.position(fixed), glm::vec2(5.0F, 100.0F));
    EXPECT_NEAR(world.position(kinematic).x, 11.0F, 1e-4F);
    EXPECT_NEAR(world.position(kinematic).y, 100.0F, 1e-6F);
    EXPECT_EQ(world.position(floating), glm::vec2(15.0F, 100.0F));
    EXPECT_EQ(world.mass(fixed), 0.0F);
    EXPECT_EQ(world.mass(kinematic), 0.0F);
}

TEST(PhysicsWorldTests, MassAndInertiaComeFromTheShapes)
{
    PhysicsWorld world;
    const BodyId box = addBox(world, {0, 0}, {1.0F, 0.5F}, {.mass = 3.0F});
    EXPECT_FLOAT_EQ(world.mass(box), 3.0F);
    EXPECT_NEAR(world.inertia(box), 3.0F * (4.0F + 1.0F) / 12.0F, 1e-4F);
    world.setMass(box, 6.0F);
    EXPECT_NEAR(world.inertia(box), 6.0F * (4.0F + 1.0F) / 12.0F, 1e-4F);

    // A shape off the origin moves the centre of mass.
    const BodyId lopsided = world.createBody({.position = {10.0F, 0.0F}, .gravityScale = 0.0F});
    world.addShape(lopsided, ShapeDef::makeCircle(0.5F, {2.0F, 0.0F}));
    EXPECT_NEAR(world.worldCenter(lopsided).x, 12.0F, 1e-5F);

    const BodyId locked = addBox(world, {0, 5}, {0.5F, 0.5F}, {.fixedRotation = true});
    world.applyImpulse(locked, {0.0F, 1.0F}, world.worldCenter(locked) + glm::vec2(0.5F, 0.0F));
    EXPECT_EQ(world.angularVelocity(locked), 0.0F);
}

TEST(PhysicsWorldTests, ImpulsesAndForces)
{
    PhysicsWorld world({.gravity = {0.0F, 0.0F}});
    const BodyId body = addBox(world, {0, 0}, {0.5F, 0.5F}, {.mass = 2.0F});
    world.applyImpulseToCenter(body, {4.0F, 0.0F});
    EXPECT_NEAR(world.velocity(body).x, 2.0F, 1e-6F);
    // An off-centre impulse also spins it.
    world.applyImpulse(body, {0.0F, 1.0F}, {0.5F, 0.0F});
    EXPECT_GT(world.angularVelocity(body), 0.0F);
    world.setAngularVelocity(body, 0.0F);
    world.applyForceToCenter(body, {0.0F, 120.0F}); // for one step: dv = F / m * dt
    world.step(kDt);
    EXPECT_NEAR(world.velocity(body).y, 0.5F + (120.0F / 2.0F * kDt), 1e-4F);
    world.step(kDt);
    EXPECT_NEAR(world.velocity(body).y, 0.5F + (120.0F / 2.0F * kDt), 1e-4F); // forces were cleared
}

// ---------------------------------------------------------------------------------------------
// Resolution

TEST(PhysicsWorldTests, BoxComesToRestOnTheGround)
{
    PhysicsWorld world;
    addGround(world);
    const BodyId box = addBox(world, {0.0F, 3.0F});
    run(world, 2.0F);
    // Resting with at most the allowed overlap.
    EXPECT_NEAR(world.position(box).y, 0.5F, 2.0F * world.settings().linearSlop);
    EXPECT_NEAR(world.velocity(box).y, 0.0F, 1e-3F);
    EXPECT_NEAR(world.angle(box), 0.0F, 1e-4F);
    EXPECT_TRUE(world.isGrounded(box));
}

TEST(PhysicsWorldTests, RestitutionOneBouncesBackRestitutionZeroDoesNot)
{
    for (const float restitution : {0.0F, 1.0F}) {
        PhysicsWorld world({.enableSleep = false});
        addGround(world);
        const BodyId ball = addBall(world, {0.0F, 5.0F}, 0.5F, {}, restitution);
        // Highest point after the first bounce (from 1.2 s on: after the impact at about 0.95 s).
        run(world, 1.2F);
        float highest = world.position(ball).y;
        for (int i = 0; i < 90; ++i) {
            world.step(kDt);
            highest = std::max(highest, world.position(ball).y);
        }
        if (restitution == 0.0F) {
            EXPECT_LT(highest, 0.55F);
        } else {
            // Back to the 5 m it fell from, neither lost nor gained; sampling every step misses
            // the true top by at most g·dt²/2 (1.4 mm).
            EXPECT_NEAR(highest, 5.0F, 0.005F);
        }
    }
}

TEST(PhysicsWorldTests, FrictionStopsASlidingBox)
{
    for (const float friction : {0.0F, 0.5F}) {
        PhysicsWorld world({.enableSleep = false});
        addGround(world, friction);
        const BodyId box = addBox(world, {0.0F, 0.5F}, glm::vec2(0.5F),
                                  {.velocity = {5.0F, 0.0F}, .fixedRotation = true}, friction);
        run(world, 2.0F);
        if (friction == 0.0F) {
            EXPECT_NEAR(world.velocity(box).x, 5.0F, 1e-3F);
        } else {
            // mu * g = 4.9 m/s² decelerates 5 m/s to rest in about 1 s, after 5 / (2 * 4.9) m.
            EXPECT_NEAR(world.velocity(box).x, 0.0F, 1e-3F);
            EXPECT_NEAR(world.position(box).x, 25.0F / (2.0F * 0.5F * 9.81F), 0.15F);
        }
    }
}

TEST(PhysicsWorldTests, KinematicBodyPushesDynamicBodies)
{
    PhysicsWorld world;
    addGround(world, 0.0F);
    const BodyId pusher = addBox(world, {-2.0F, 0.5F}, glm::vec2(0.5F),
                                 {.type = BodyType::Kinematic, .velocity = {2.0F, 0.0F}});
    const BodyId box = addBox(world, {0.0F, 0.5F}, glm::vec2(0.5F), {.fixedRotation = true}, 0.0F);
    run(world, 2.0F);
    EXPECT_NEAR(world.position(pusher).x, 2.0F, 1e-3F);
    EXPECT_GT(world.position(box).x, world.position(pusher).x + 0.95F); // in front, not overlapped
}

TEST(PhysicsWorldTests, StackOfTenBoxesStaysStandingForTenSeconds)
{
    PhysicsWorld world({.enableSleep = false});
    addGround(world);
    const std::vector<BodyId> stack = addStack(world, 10);
    run(world, 10.0F);
    for (std::size_t i = 0; i < stack.size(); ++i) {
        const glm::vec2 p = world.position(stack[i]);
        EXPECT_NEAR(p.x, 0.0F, 0.01F) << "box " << i;
        // Each contact may overlap by about the slop.
        EXPECT_NEAR(p.y, 0.5F + static_cast<float>(i), 0.02F + (0.01F * static_cast<float>(i)))
            << "box " << i;
        EXPECT_NEAR(world.angle(stack[i]), 0.0F, 0.01F) << "box " << i;
    }
}

TEST(PhysicsWorldTests, RotatedBoxFallsFlat)
{
    PhysicsWorld world;
    addGround(world);
    const BodyId box = addBox(world, {0.0F, 2.0F}, glm::vec2(0.5F), {.angle = 0.3F});
    run(world, 4.0F);
    // Lands on a corner, tips over and lies on a side: a multiple of 90°.
    const float quarter = std::numbers::pi_v<float> / 2.0F;
    const float angle = world.angle(box);
    EXPECT_NEAR(angle - (quarter * std::round(angle / quarter)), 0.0F, 0.01F);
    EXPECT_NEAR(world.position(box).y, 0.5F, 0.02F);
}

TEST(PhysicsWorldTests, BallRollsDownASlope)
{
    PhysicsWorld world({.enableSleep = false});
    const BodyId slope = world.createBody({.type = BodyType::Static, .angle = -0.3F});
    world.addShape(slope, ShapeDef::makeBox({20.0F, 0.5F}));
    const BodyId ball = addBall(world, {0.0F, 1.5F});
    run(world, 2.0F);
    EXPECT_GT(world.position(ball).x, 1.0F);
    // Rolling without sliding: the contact point does not move, so speed = -ω·r.
    const float speed = glm::length(world.velocity(ball));
    EXPECT_GT(speed, 1.0F);
    EXPECT_NEAR(-world.angularVelocity(ball) * 0.5F, speed, 0.02F * speed);
}

TEST(PhysicsWorldTests, ProjectileRangeMatchesTheAnalyticRange)
{
    // Launched at speed v and angle θ, a body comes back to its start height at R = v²·sin 2θ / g.
    // Semi-implicit Euler adds gravity to the velocity before moving, so after n steps
    // y = vy·n·dt - g·dt²·n(n+1)/2: it lands exactly one step early, at R - vx·dt.
    const float v = 10.0F;
    const float g = 9.81F;
    for (const float theta : {0.3F, std::numbers::pi_v<float> / 4.0F, 1.2F}) {
        PhysicsWorld world;
        const glm::vec2 velocity = v * glm::vec2(std::cos(theta), std::sin(theta));
        const BodyId ball = addBall(world, {0.0F, 0.0F}, 0.1F, {.velocity = velocity});
        glm::vec2 previous = world.position(ball);
        while (true) {
            world.step(kDt);
            const glm::vec2 now = world.position(ball);
            if (now.y < 0.0F) { // crossed the start height: interpolate where
                const float x =
                    previous.x + ((now.x - previous.x) * previous.y / (previous.y - now.y));
                EXPECT_NEAR(x, (v * v * std::sin(2.0F * theta) / g) - (velocity.x * kDt), 0.01F)
                    << theta;
                break;
            }
            previous = now;
        }
    }
}

TEST(PhysicsWorldTests, HeadOnCollisionsMatchTheAnalyticVelocities)
{
    // Two balls on a line, masses 1 and 3: momentum is kept, and the relative speed after the
    // impact is e times the one before:
    //   v1' = (m1·v1 + m2·v2 - m2·e·(v1 - v2)) / (m1 + m2), v2' likewise with +m1·e.
    const float m1 = 1.0F;
    const float m2 = 3.0F;
    const float v1 = 2.0F;
    const float v2 = -1.0F;
    for (const float e : {0.0F, 0.5F, 1.0F}) {
        PhysicsWorld world({.gravity = {0.0F, 0.0F}});
        const BodyId a =
            addBall(world, {0.0F, 0.0F}, 0.5F, {.velocity = {v1, 0.0F}, .mass = m1}, e);
        const BodyId b =
            addBall(world, {3.0F, 0.0F}, 0.5F, {.velocity = {v2, 0.0F}, .mass = m2}, e);
        run(world, 1.0F); // they meet at about 0.67 s
        const float momentum = (m1 * v1) + (m2 * v2);
        const float after1 = (momentum - (m2 * e * (v1 - v2))) / (m1 + m2);
        const float after2 = (momentum + (m1 * e * (v1 - v2))) / (m1 + m2);
        EXPECT_NEAR(world.velocity(a).x, after1, 1e-3F) << "e = " << e;
        EXPECT_NEAR(world.velocity(b).x, after2, 1e-3F) << "e = " << e;
        EXPECT_NEAR((m1 * world.velocity(a).x) + (m2 * world.velocity(b).x), momentum, 1e-3F);
    }
}

TEST(PhysicsWorldTests, FrictionHoldsABoxOnASlopeUntilTanThetaExceedsMu)
{
    // A box at rest on a slope of angle θ with friction μ = 0.5 stays while tan θ < μ, and
    // otherwise slides down with acceleration g·(sin θ - μ·cos θ).
    const float mu = 0.5F;
    const float g = 9.81F;
    for (const float theta : {std::atan(mu) - 0.1F, std::atan(mu) + 0.15F}) {
        PhysicsWorld world({.enableSleep = false});
        const BodyId slope = world.createBody({.type = BodyType::Static, .angle = theta});
        ShapeDef def = ShapeDef::makeBox({20.0F, 0.5F});
        def.friction = mu;
        world.addShape(slope, def);
        const glm::vec2 up(-std::sin(theta), std::cos(theta)); // the slope's normal
        const BodyId box = addBox(world, up * 1.0F, glm::vec2(0.5F), {.angle = theta}, mu);
        run(world, 1.0F);
        const float speed1 = glm::length(world.velocity(box));
        run(world, 1.0F);
        const float speed2 = glm::length(world.velocity(box));
        if (std::tan(theta) < mu) {
            EXPECT_LT(speed2, 1e-3F) << "theta " << theta;
        } else {
            const float expected = g * (std::sin(theta) - (mu * std::cos(theta)));
            EXPECT_NEAR(speed2 - speed1, expected, 0.02F * expected) << "theta " << theta;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Determinism and broad phases

namespace {

std::vector<glm::vec2> simulatePile(astra2d::BroadPhaseType broadPhase)
{
    PhysicsWorld world({.broadPhase = broadPhase});
    addGround(world);
    std::vector<BodyId> bodies;
    for (int i = 0; i < 60; ++i) {
        const int column = i % 6;
        const int row = i / 6;
        const glm::vec2 p((static_cast<float>(column) * 1.1F) - 3.0F +
                              (0.13F * static_cast<float>(i % 3)),
                          1.0F + (static_cast<float>(row) * 1.2F));
        bodies.push_back(
            i % 2 == 0 ? addBox(world, p, {0.4F, 0.3F}, {.angle = 0.1F * static_cast<float>(i % 4)})
                       : addBall(world, p, 0.35F));
    }
    run(world, 3.0F);
    std::vector<glm::vec2> result;
    for (const BodyId body : bodies) {
        result.push_back(world.position(body));
        result.emplace_back(world.angle(body), 0.0F);
    }
    return result;
}

} // namespace

TEST(PhysicsWorldTests, SteppingIsDeterministicWhateverTheBroadPhase)
{
    const auto first = simulatePile(astra2d::BroadPhaseType::UniformGrid);
    EXPECT_EQ(simulatePile(astra2d::BroadPhaseType::UniformGrid), first); // bit for bit
    EXPECT_EQ(simulatePile(astra2d::BroadPhaseType::SortAndSweep), first);
    EXPECT_EQ(simulatePile(astra2d::BroadPhaseType::BruteForce), first);
}

TEST(PhysicsWorldTests, StatsCountTheBroadPhaseWork)
{
    PhysicsWorld world({.broadPhase = astra2d::BroadPhaseType::BruteForce});
    addGround(world);
    for (int i = 0; i < 20; ++i) {
        addBall(world, {static_cast<float>(i) * 2.0F, 0.4F});
    }
    world.step(kDt);
    EXPECT_EQ(world.stats().bodies, 21);
    EXPECT_EQ(world.stats().shapes, 21);
    EXPECT_EQ(world.stats().pairTests, 21 * 20 / 2);
    EXPECT_EQ(world.stats().candidatePairs, 20); // each ball with the ground
    EXPECT_EQ(world.stats().touchingContacts, 20);
    world.settings().broadPhase = astra2d::BroadPhaseType::SortAndSweep;
    world.step(kDt);
    EXPECT_LT(world.stats().pairTests, 21 * 20 / 2);
    EXPECT_EQ(world.stats().candidatePairs, 20);
}

// ---------------------------------------------------------------------------------------------
// Events, triggers, filtering

TEST(PhysicsWorldTests, ContactEventsBeginAndEnd)
{
    PhysicsWorld world({.enableSleep = false});
    const BodyId ground = addGround(world);
    const BodyId ball = addBall(world, {0.0F, 2.0F});
    int begins = 0;
    float speed = 0.0F;
    for (int i = 0; i < 60 && begins == 0; ++i) {
        world.step(kDt);
        for (const auto& event : world.contactBeginEvents()) {
            ++begins;
            speed = event.approachSpeed;
            EXPECT_TRUE((event.bodyA == ground && event.bodyB == ball) ||
                        (event.bodyA == ball && event.bodyB == ground));
            // The normal points from A to B: up if the ground is A.
            EXPECT_NEAR(event.bodyA == ground ? event.normal.y : -event.normal.y, 1.0F, 1e-4F);
        }
    }
    EXPECT_EQ(begins, 1);
    EXPECT_NEAR(speed, std::sqrt(2.0F * 9.81F * 1.5F), 0.3F); // falling 1.5 m

    world.applyImpulseToCenter(ball, {0.0F, 10.0F});
    int ends = 0;
    for (int i = 0; i < 10; ++i) {
        world.step(kDt);
        ends += static_cast<int>(world.contactEndEvents().size());
    }
    EXPECT_EQ(ends, 1);
}

TEST(PhysicsWorldTests, TriggersReportOverlapsWithoutColliding)
{
    PhysicsWorld world;
    addGround(world);
    const BodyId zone = world.createBody({.type = BodyType::Static, .position = {0.0F, 3.0F}});
    ShapeDef def = ShapeDef::makeBox({2.0F, 0.5F});
    def.isTrigger = true;
    const ShapeId trigger = world.addShape(zone, def);
    const BodyId ball = addBall(world, {0.0F, 5.0F}, 0.25F);
    int enters = 0;
    int exits = 0;
    for (int i = 0; i < 120; ++i) {
        world.step(kDt);
        for (const auto& event : world.triggerBeginEvents()) {
            ++enters;
            EXPECT_EQ(event.trigger, trigger);
            EXPECT_EQ(event.otherBody, ball);
        }
        exits += static_cast<int>(world.triggerEndEvents().size());
        EXPECT_TRUE(world.contactBeginEvents().empty() || world.position(ball).y < 1.0F);
    }
    EXPECT_EQ(enters, 1);
    EXPECT_EQ(exits, 1);
    EXPECT_NEAR(world.position(ball).y, 0.25F, 0.02F); // fell through the zone onto the ground
}

TEST(PhysicsWorldTests, KinematicBodiesEnterStaticTriggers)
{
    PhysicsWorld world({.gravity = {0.0F, 0.0F}});
    const BodyId zone = world.createBody({.type = BodyType::Static, .position = {3.0F, 0.0F}});
    ShapeDef def = ShapeDef::makeBox({0.5F, 0.5F});
    def.isTrigger = true;
    world.addShape(zone, def);
    const BodyId platform = world.createBody(
        {.type = BodyType::Kinematic, .position = {0.0F, 0.0F}, .velocity = {2.0F, 0.0F}});
    world.addShape(platform, ShapeDef::makeBox({0.5F, 0.1F}));
    int enters = 0;
    int exits = 0;
    for (int i = 0; i < 240; ++i) { // 4 s: from x = 0 through the zone to x = 8
        world.step(kDt);
        for (const auto& event : world.triggerBeginEvents()) {
            ++enters;
            EXPECT_EQ(event.otherBody, platform);
        }
        exits += static_cast<int>(world.triggerEndEvents().size());
    }
    EXPECT_EQ(enters, 1);
    EXPECT_EQ(exits, 1);
}

// Built at compile time.
static_assert(astra2d::collisionGroups(1U << 0U, 1U << 3U) == 0b1001U);
static_assert(astra2d::collisionGroups() == 0U);
static_assert(astra2d::withoutGroups(0b1011U, 0b0010U) == 0b1001U);
static_assert(astra2d::withoutGroups(0xFFFFU, 0x8000U) == 0x7FFFU);

TEST(PhysicsWorldTests, CategoryAndMaskFilterPairs)
{
    PhysicsWorld world;
    const BodyId ground = world.createBody({.type = BodyType::Static, .position = {0.0F, -0.5F}});
    ShapeDef floor = ShapeDef::makeBox({10.0F, 0.5F});
    floor.category = 0x0002;
    world.addShape(ground, floor);
    const BodyId ghost = world.createBody({.position = {0.0F, 1.0F}});
    ShapeDef ghostShape = ShapeDef::makeCircle(0.5F);
    ghostShape.mask = 0x0001; // ignores category 2
    world.addShape(ghost, ghostShape);
    run(world, 1.0F);
    EXPECT_LT(world.position(ghost).y, -1.0F); // fell through
}

TEST(PhysicsWorldTests, DestroyingTheGroundEndsContactsAndWakesBodies)
{
    PhysicsWorld world;
    const BodyId ground = addGround(world);
    const BodyId box = addBox(world, {0.0F, 0.5F});
    run(world, 2.0F);
    ASSERT_FALSE(world.isAwake(box)); // at rest long enough
    world.destroyBody(ground);
    EXPECT_TRUE(world.isAwake(box));
    world.step(kDt);
    ASSERT_EQ(world.contactEndEvents().size(), 1U);
    EXPECT_FALSE(world.valid(world.contactEndEvents()[0].bodyA)); // the destroyed ground
    run(world, 0.5F);
    EXPECT_LT(world.position(box).y, 0.0F);
}

// ---------------------------------------------------------------------------------------------
// Sleeping

TEST(PhysicsWorldTests, StackSleepsAsAnIslandAndWakesWhenHit)
{
    PhysicsWorld world;
    addGround(world);
    const std::vector<BodyId> stack = addStack(world, 3);
    run(world, 3.0F);
    for (const BodyId box : stack) {
        EXPECT_FALSE(world.isAwake(box));
    }
    EXPECT_EQ(world.stats().awakeBodies, 0);
    // A ball dropped on top wakes the whole stack, not just the top box.
    addBall(world, {0.0F, 4.0F}, 0.3F);
    bool allAwake = false;
    for (int i = 0; i < 60 && !allAwake; ++i) {
        world.step(kDt);
        allAwake = world.isAwake(stack[0]) && world.isAwake(stack[1]) && world.isAwake(stack[2]);
    }
    EXPECT_TRUE(allAwake);
}

TEST(PhysicsWorldTests, SettersWakeAndSleepCanBeDisabled)
{
    PhysicsWorld world;
    addGround(world);
    const BodyId box = addBox(world, {0.0F, 0.5F});
    const BodyId restless = addBox(world, {3.0F, 0.5F}, glm::vec2(0.5F), {.allowSleep = false});
    run(world, 2.0F);
    EXPECT_FALSE(world.isAwake(box));
    EXPECT_TRUE(world.isAwake(restless));
    world.setVelocity(box, {1.0F, 0.0F});
    EXPECT_TRUE(world.isAwake(box));
    world.setAwake(box, false);
    EXPECT_FALSE(world.isAwake(box));
    EXPECT_EQ(world.velocity(box), glm::vec2(0.0F));
}

// ---------------------------------------------------------------------------------------------
// One-way platforms and bullets

TEST(PhysicsWorldTests, OneWayPlatformLetsBodiesUpAndHoldsThemOnTop)
{
    PhysicsWorld world({.enableSleep = false});
    const BodyId platform = world.createBody({.type = BodyType::Static, .position = {0.0F, 3.0F}});
    ShapeDef def = ShapeDef::makeBox({2.0F, 0.1F});
    def.oneWay = true;
    world.addShape(platform, def);
    // Thrown up from below: passes through, then lands on top.
    const BodyId ball = addBall(world, {0.0F, 1.0F}, 0.25F, {.velocity = {0.0F, 9.0F}});
    float highest = 0.0F;
    run(world, 0.1F);
    for (int i = 0; i < 180; ++i) {
        world.step(kDt);
        highest = std::max(highest, world.position(ball).y);
    }
    EXPECT_GT(highest, 3.5F);
    EXPECT_NEAR(world.position(ball).y, 3.1F + 0.25F, 0.02F);
    EXPECT_TRUE(world.isGrounded(ball));
}

TEST(PhysicsWorldTests, OneWayPlatformHoldsABodyThatBarelyReachedItsTop)
{
    PhysicsWorld world({.enableSleep = false});
    const BodyId platform = world.createBody({.type = BodyType::Static, .position = {0.0F, 3.0F}});
    ShapeDef def = ShapeDef::makeBox({2.0F, 0.25F});
    def.oneWay = true;
    world.addShape(platform, def);
    // Thrown up from below so that at the top of its flight its bottom is still a little inside
    // the platform (about 0.15 of 0.5): it falls back onto the top instead of through.
    const BodyId ball = addBall(world, {0.0F, 1.0F}, 0.25F, {.velocity = {0.0F, 6.86F}});
    float highest = 0.0F;
    for (int i = 0; i < 180; ++i) {
        world.step(kDt);
        highest = std::max(highest, world.position(ball).y);
    }
    EXPECT_LT(highest, 3.25F + 0.25F); // never fully above the top
    EXPECT_NEAR(world.position(ball).y, 3.25F + 0.25F, 0.02F);
    EXPECT_TRUE(world.isGrounded(ball));
}

TEST(PhysicsWorldTests, OneWayPlatformHoldsABodyLandingOnItsVeryEnd)
{
    PhysicsWorld world({.enableSleep = false});
    const BodyId platform = world.createBody({.type = BodyType::Static, .position = {0.0F, 3.0F}});
    ShapeDef def = ShapeDef::makeBox({1.5F, 0.22F});
    def.oneWay = true;
    world.addShape(platform, def);
    // Falling fast with only 0.08 of its width over the platform's end: in its first step in
    // contact it sinks in about 0.3, deeper than that, but must land on top, not be pushed off
    // sideways or fall through.
    const BodyId box = world.createBody({.type = BodyType::Dynamic,
                                         .position = {1.70F, 3.8F},
                                         .velocity = {0.0F, -26.0F},
                                         .fixedRotation = true}); // like a character
    world.addShape(box, ShapeDef::makeBox({0.28F, 0.44F}));
    run(world, 1.0F);
    EXPECT_NEAR(world.position(box).y, 3.22F + 0.44F, 0.02F);
    EXPECT_TRUE(world.isGrounded(box));
}

TEST(PhysicsWorldTests, OneWayPlatformHoldsABodyWhoseVelocityTheGameCapsEveryStep)
{
    // As in the platformer: a character whose fall speed the game caps before every step, while
    // gravity adds to it during the step. It sinks into the platform as far as it really moved,
    // which is more than its capped velocity says. Dropped from many heights, so the step in
    // which it reaches the platform lands at every depth.
    const float maxFall = 420.0F;
    for (int i = 0; i < 40; ++i) {
        PhysicsWorld world({.gravity = {0.0F, -1200.0F}, .lengthUnitsPerMeter = 16.0F});
        const BodyId platform =
            world.createBody({.type = BodyType::Static, .position = {0.0F, 0.0F}});
        ShapeDef def = ShapeDef::makeBox({24.0F, 3.5F});
        def.oneWay = true;
        def.friction = 0.0F;
        world.addShape(platform, def);
        const float start = 60.0F + (0.2F * static_cast<float>(i));
        const BodyId box = world.createBody(
            {.position = {0.0F, start}, .gravityScale = 1.7F, .fixedRotation = true});
        world.addShape(box, ShapeDef::makeBox({6.0F, 9.0F}));
        for (int step = 0; step < 120; ++step) {
            const glm::vec2 v = world.velocity(box);
            world.setVelocity(box, {v.x, std::max(v.y, -maxFall)});
            world.step(kDt);
        }
        EXPECT_NEAR(world.position(box).y, 3.5F + 9.0F, 0.5F) << "dropped from " << start;
    }
}

TEST(PhysicsWorldTests, OneWayMovingPlatformsCarryABodyLandingOnThem)
{
    // The platformer's lifts and platforms: kinematic, given each step the velocity that reaches
    // the next point of a smooth back-and-forth path, so their velocity changes every step too.
    // A character dropped onto them at different moments must land and ride along.
    const float period = 4.0F;
    for (const glm::vec2 travel : {glm::vec2(0.0F, 64.0F), glm::vec2(64.0F, 0.0F)}) {
        for (int i = 0; i < 20; ++i) {
            PhysicsWorld world({.gravity = {0.0F, -1200.0F}, .lengthUnitsPerMeter = 16.0F});
            const BodyId lift = world.createBody({.type = BodyType::Kinematic});
            ShapeDef def = ShapeDef::makeBox({24.0F, 3.5F});
            def.oneWay = true;
            def.friction = 0.0F;
            world.addShape(lift, def);
            const BodyId box =
                world.createBody({.position = {0.0F, 40.0F + (3.0F * static_cast<float>(i))},
                                  .gravityScale = 1.7F,
                                  .fixedRotation = true});
            world.addShape(box, ShapeDef::makeBox({6.0F, 9.0F}));
            float time = 0.0F;
            for (int step = 0; step < 600; ++step) { // 10 s, two and a half periods
                time += kDt;
                const float phase =
                    0.5F - (0.5F * std::cos(2.0F * std::numbers::pi_v<float> * time / period));
                world.setVelocity(lift, ((travel * phase) - world.position(lift)) / kDt);
                const glm::vec2 v = world.velocity(box);
                // Carried sideways, as the game's controller does, and the fall speed capped.
                world.setVelocity(box, {world.velocity(lift).x, std::max(v.y, -420.0F)});
                world.step(kDt);
            }
            EXPECT_NEAR(world.position(box).y - world.position(lift).y, 3.5F + 9.0F, 1.0F)
                << "travel " << travel.x << "," << travel.y << ", drop " << i;
        }
    }
}

TEST(PhysicsWorldTests, BulletsDoNotTunnelThroughThinWalls)
{
    for (const bool bullet : {false, true}) {
        PhysicsWorld world({.gravity = {0.0F, 0.0F}});
        const BodyId wall = world.createBody({.type = BodyType::Static, .position = {5.0F, 0.0F}});
        world.addShape(wall, ShapeDef::makeBox({0.05F, 2.0F}));
        // 300 m/s: 5 m per step, far more than the wall is thick.
        const BodyId shot =
            addBall(world, {0.0F, 0.0F}, 0.1F, {.velocity = {300.0F, 0.0F}, .bullet = bullet});
        run(world, 0.1F);
        if (bullet) {
            EXPECT_LT(world.position(shot).x, 5.0F);
            EXPECT_GT(world.position(shot).x, 4.7F); // stopped at the wall
        } else {
            EXPECT_GT(world.position(shot).x, 10.0F); // tunnelled
        }
    }
}

TEST(PhysicsWorldTests, BoxBulletsDoNotTunnelWhenTheyHitAWallDiagonally)
{
    // Starts above the wall's top and reaches its face at an angle, axis-aligned and rotated.
    for (const float angle : {0.0F, 0.3F}) {
        PhysicsWorld world({.gravity = {0.0F, 0.0F}});
        const BodyId wall = world.createBody({.type = BodyType::Static, .position = {5.0F, 0.0F}});
        world.addShape(wall, ShapeDef::makeBox({0.05F, 2.0F}));
        const BodyId shot = addBox(world, {-2.5F, 4.0F}, glm::vec2(0.1F),
                                   {.angle = angle, .velocity = {300.0F, -90.0F}, .bullet = true});
        run(world, 0.1F);
        EXPECT_LT(world.position(shot).x, 5.0F) << "angle " << angle;
    }
}

// ---------------------------------------------------------------------------------------------
// Queries

TEST(PhysicsWorldTests, RayCastFindsTheClosestShapeAndSkipsTriggers)
{
    PhysicsWorld world;
    addGround(world);
    const BodyId near = addBox(world, {3.0F, 0.5F}, glm::vec2(0.5F), {.type = BodyType::Static});
    addBox(world, {6.0F, 0.5F}, glm::vec2(0.5F), {.type = BodyType::Static});
    const BodyId zone = world.createBody({.type = BodyType::Static, .position = {1.0F, 0.5F}});
    ShapeDef trigger = ShapeDef::makeBox(glm::vec2(0.5F));
    trigger.isTrigger = true;
    world.addShape(zone, trigger);

    const auto hit = world.rayCast({0.0F, 0.5F}, {10.0F, 0.0F});
    ASSERT_TRUE(hit.has_value());
    const astra2d::WorldRayHit h = hit.value_or(astra2d::WorldRayHit{});
    EXPECT_EQ(h.body, near);
    EXPECT_NEAR(h.hit.point.x, 2.5F, 1e-4F);
    EXPECT_FALSE(world.rayCast({0.0F, 5.0F}, {10.0F, 0.0F}).has_value());
    EXPECT_FALSE(world.rayCast({0.0F, 0.5F}, {10.0F, 0.0F}, 0x0002).has_value()); // masked out

    EXPECT_EQ(world.queryPoint({3.2F, 0.7F}).size(), 1U);
    EXPECT_EQ(world.queryPoint({1.0F, 0.5F}).size(), 1U); // the trigger
    EXPECT_TRUE(world.queryPoint({4.5F, 2.0F}).empty());
    EXPECT_EQ(world.queryAabb({{2.0F, 0.2F}, {6.2F, 0.4F}}).size(),
              2U); // both boxes, not the ground
    EXPECT_EQ(world.queryAabb({{2.0F, -0.2F}, {6.2F, 0.4F}}).size(), 3U); // and the ground
}

TEST(PhysicsWorldTests, IsGroundedOnlyOnFloors)
{
    PhysicsWorld world({.gravity = {0.0F, 0.0F}});
    addGround(world);
    const BodyId wall = world.createBody({.type = BodyType::Static, .position = {5.0F, 5.0F}});
    world.addShape(wall, ShapeDef::makeBox({0.5F, 5.0F}));
    const BodyId onFloor = addBox(world, {0.0F, 0.49F});
    const BodyId atWall = addBox(world, {4.01F, 5.0F});
    const BodyId flying = addBox(world, {0.0F, 3.0F});
    world.step(kDt);
    EXPECT_TRUE(world.isGrounded(onFloor));
    EXPECT_FALSE(world.isGrounded(atWall));
    EXPECT_FALSE(world.isGrounded(flying));
    ASSERT_EQ(world.contacts(atWall).size(), 1U);
    EXPECT_NEAR(world.contacts(atWall)[0].manifold.normal.x, 1.0F, 1e-5F); // away from the body
}

// ---------------------------------------------------------------------------------------------
// Tile maps

namespace {

astra2d::TileGrid parseGrid(const std::vector<std::string>& rows)
{
    // Rows are given top first, as they look; '#' solid, '-' one-way.
    astra2d::TileGrid grid;
    grid.height = static_cast<int>(rows.size());
    grid.width = static_cast<int>(rows.front().size());
    grid.cells.assign(rows.size() * rows.front().size(), astra2d::TileCollision::Empty);
    for (int y = 0; y < grid.height; ++y) {
        const std::string& row = rows[static_cast<std::size_t>(grid.height - 1 - y)];
        for (int x = 0; x < grid.width; ++x) {
            const char c = row[static_cast<std::size_t>(x)];
            if (c == '#') {
                grid.set(x, y, astra2d::TileCollision::Solid);
            } else if (c == '-') {
                grid.set(x, y, astra2d::TileCollision::OneWay);
            }
        }
    }
    return grid;
}

} // namespace

TEST(TilemapTests, MergesRunsAndMasksInternalEdges)
{
    const astra2d::TileGrid grid = parseGrid({
        "..---...",
        "#......#",
        "##.....#",
        "########",
    });
    const auto rects = astra2d::mergeTiles(grid);
    // Bottom row: one run. Left column: rows 1 and 2 differ in width, so 2 runs. Right: 1 run of
    // height 2 (same column in rows 1-2). The platform: 1 run.
    ASSERT_EQ(rects.size(), 5U);
    const astra2d::TileRect& floor = rects[0];
    EXPECT_EQ(floor.width, 8);
    EXPECT_EQ(floor.height, 1);
    // Its top is partly open (walls stand on it): it stays enabled; its bottom is the map border.
    EXPECT_EQ(floor.edgeMask,
              astra2d::kBoxBottom | astra2d::kBoxTop | astra2d::kBoxLeft | astra2d::kBoxRight);
    const auto right =
        std::ranges::find_if(rects, [](const auto& r) { return r.x == 7 && r.y == 1; });
    ASSERT_NE(right, rects.end());
    EXPECT_EQ(right->height, 2);
    EXPECT_EQ(right->edgeMask & astra2d::kBoxBottom, 0U); // stands on the floor: internal
    const auto platform = std::ranges::find_if(
        rects, [](const auto& r) { return r.kind == astra2d::TileCollision::OneWay; });
    ASSERT_NE(platform, rects.end());
    EXPECT_EQ(platform->width, 3);
    EXPECT_EQ(platform->edgeMask, astra2d::kBoxTop);
}

TEST(TilemapTests, BoxSlidesAlongATiledWallWithoutCatching)
{
    // A column of tiles is merged into one rectangle: no seams at all.
    PhysicsWorld world;
    const BodyId level = world.createBody({.type = BodyType::Static});
    EXPECT_EQ(astra2d::addTileShapes(world, level, parseGrid({"#", "#", "#", "#"})).size(), 1U);

    // One shape per tile, with the internal edges masked as mergeTiles() would, and a box pushed
    // against the wall (gravity also points left) sliding down along it. Where it enters the next
    // tile from above, the tile's top edge would be the axis of least overlap and stop it.
    PhysicsWorld tiles({.gravity = {-5.0F, -9.81F}, .enableSleep = false});
    const BodyId level2 = tiles.createBody({.type = BodyType::Static});
    for (int y = 0; y < 8; ++y) {
        ShapeDef tile = ShapeDef::makeBox(glm::vec2(0.5F), {0.5F, static_cast<float>(y) + 0.5F});
        tile.friction = 0.0F;
        unsigned mask = astra2d::kBoxRight;
        mask |= y == 7 ? unsigned{astra2d::kBoxTop} : 0U;
        mask |= y == 0 ? unsigned{astra2d::kBoxBottom} : 0U;
        tile.edgeMask = static_cast<std::uint8_t>(mask);
        tiles.addShape(level2, tile);
    }
    const BodyId box = addBox(tiles, {1.5F, 7.0F}, glm::vec2(0.5F),
                              {.velocity = {0.0F, -1.0F}, .fixedRotation = true}, 0.0F);
    float previous = tiles.velocity(box).y;
    for (int i = 0; i < 40; ++i) {
        tiles.step(kDt);
        const float v = tiles.velocity(box).y;
        EXPECT_LT(v, previous + 1e-4F) << "caught on a seam at step " << i; // keeps speeding up
        previous = v;
    }
    EXPECT_NEAR(tiles.position(box).x, 1.5F, 0.02F); // still against the wall
}

TEST(TilemapTests, BodyLandsOnTilesAndOneWayTiles)
{
    PhysicsWorld world;
    const astra2d::TileGrid grid = parseGrid({
        "........",
        "...---..",
        "........",
        "########",
    });
    const BodyId level = world.createBody({.type = BodyType::Static});
    astra2d::addTileShapes(world, level, grid);
    const BodyId onFloor = addBox(world, {1.0F, 3.0F}, glm::vec2(0.4F));
    const BodyId onPlatform = addBox(world, {4.5F, 4.0F}, glm::vec2(0.4F));
    run(world, 2.0F);
    EXPECT_NEAR(world.position(onFloor).y, 1.4F, 0.02F);
    EXPECT_NEAR(world.position(onPlatform).y, 3.4F, 0.02F);
    EXPECT_TRUE(world.isGrounded(onPlatform));
}

TEST(TilemapTests, OneWayTilesLetBodiesThroughFromTheSide)
{
    // A top-only tile's normal is always up; a body entering it sideways must not be lifted.
    PhysicsWorld world({.gravity = {0.0F, 0.0F}, .enableSleep = false});
    const astra2d::TileGrid grid = parseGrid({
        "...---..",
        "........",
    });
    const BodyId level = world.createBody({.type = BodyType::Static});
    astra2d::addTileShapes(world, level, grid);
    const BodyId box = addBox(world, {0.5F, 1.5F}, glm::vec2(0.3F),
                              {.velocity = {4.0F, 0.0F}, .fixedRotation = true});
    run(world, 1.5F);
    EXPECT_GT(world.position(box).x, 6.0F);          // went through
    EXPECT_NEAR(world.position(box).y, 1.5F, 1e-3F); // at the same height
    EXPECT_NEAR(world.velocity(box).x, 4.0F, 1e-3F); // without being slowed down
}

TEST(TilemapTests, GridWithoutCellsIsEmpty)
{
    astra2d::TileGrid grid;
    grid.width = 8;
    grid.height = 4;
    EXPECT_EQ(grid.at(3, 2), astra2d::TileCollision::Empty);
    EXPECT_TRUE(astra2d::mergeTiles(grid).empty());
    grid.set(1, 1, astra2d::TileCollision::Solid);
    EXPECT_EQ(grid.at(1, 1), astra2d::TileCollision::Solid);
    EXPECT_EQ(astra2d::mergeTiles(grid).size(), 1U);
}

TEST(PhysicsWorldTests, RemoveShapesEndsContactsAndKeepsTheBody)
{
    PhysicsWorld world;
    addGround(world);
    const BodyId level = world.createBody({.type = BodyType::Static});
    world.addShape(level, ShapeDef::makeBox({0.5F, 0.5F}, {-3.0F, 0.5F}));
    world.addShape(level, ShapeDef::makeBox({0.5F, 0.5F}, {3.0F, 0.5F}));
    const BodyId left = addBox(world, {-3.0F, 1.5F}, glm::vec2(0.5F));
    const BodyId right = addBox(world, {3.0F, 1.5F}, glm::vec2(0.5F));
    run(world, 1.0F);
    ASSERT_EQ(world.shapes(level).size(), 2U);
    world.removeShapes(level);
    EXPECT_TRUE(world.valid(level));
    EXPECT_TRUE(world.shapes(level).empty());
    world.step(kDt);
    int ended = 0;
    for (const astra2d::ContactEndEvent& event : world.contactEndEvents()) {
        ended += event.bodyA == level || event.bodyB == level ? 1 : 0;
    }
    EXPECT_EQ(ended, 2);
    run(world, 1.0F);
    EXPECT_NEAR(world.position(left).y, 0.5F, 0.02F); // fell onto the ground
    EXPECT_NEAR(world.position(right).y, 0.5F, 0.02F);
}

TEST(PhysicsWorldTests, RemoveShapeEndsOnlyItsContactsAndUpdatesTheMass)
{
    PhysicsWorld world;
    addGround(world);
    const BodyId level = world.createBody({.type = BodyType::Static});
    const ShapeId leftStep = world.addShape(level, ShapeDef::makeBox({0.5F, 0.5F}, {-3.0F, 0.5F}));
    world.addShape(level, ShapeDef::makeBox({0.5F, 0.5F}, {3.0F, 0.5F}));
    const BodyId left = addBox(world, {-3.0F, 1.5F}, glm::vec2(0.5F));
    const BodyId right = addBox(world, {3.0F, 1.5F}, glm::vec2(0.5F));
    run(world, 1.0F);
    world.removeShape(leftStep);
    EXPECT_FALSE(world.valid(leftStep));
    EXPECT_EQ(world.shapes(level).size(), 1U);
    world.step(kDt);
    ASSERT_EQ(world.contactEndEvents().size(), 1U);
    const astra2d::ContactEndEvent& ended = world.contactEndEvents()[0];
    EXPECT_TRUE(ended.shapeA == leftStep || ended.shapeB == leftStep);
    run(world, 1.0F);
    EXPECT_NEAR(world.position(left).y, 0.5F, 0.02F);  // fell onto the ground
    EXPECT_NEAR(world.position(right).y, 1.5F, 0.02F); // still on its step

    // A dynamic body's centre of mass follows the shapes it has left.
    const BodyId pair = world.createBody({.position = {0.0F, 20.0F}});
    const ShapeId first = world.addShape(pair, ShapeDef::makeCircle(0.5F, {-1.0F, 0.0F}));
    world.addShape(pair, ShapeDef::makeCircle(0.5F, {1.0F, 0.0F}));
    EXPECT_NEAR(world.worldCenter(pair).x, 0.0F, 1e-5F);
    world.removeShape(first);
    EXPECT_NEAR(world.worldCenter(pair).x, 1.0F, 1e-5F);
}

TEST(PhysicsWorldTests, MovingABodyWakesWhatTouchesItBeforeTheNextQuery)
{
    PhysicsWorld world;
    const BodyId ground = addGround(world);
    const BodyId box = addBox(world, {0.0F, 0.5F});
    run(world, 3.0F);
    ASSERT_FALSE(world.isAwake(box));
    world.setTransform(ground, {0.0F, -0.6F}, 0.0F);
    EXPECT_TRUE(world.isAwake(box));
    // Putting it back to sleep afterwards sticks: the queued wake does not undo it.
    world.setTransform(ground, {0.0F, -0.5F}, 0.0F);
    world.setAwake(box, false);
    EXPECT_FALSE(world.isAwake(box));
}
