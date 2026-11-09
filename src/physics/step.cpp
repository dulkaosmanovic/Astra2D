// One step of PhysicsWorld: candidate pairs from the broad phase, contacts and their events,
// one-way platforms, triggers, islands and sleep. The solver itself is in solver.cpp.

#include "world_impl.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <utility>

namespace astra2d {

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::uint64_t pairKey(std::uint32_t a, std::uint32_t b)
{
    return (static_cast<std::uint64_t>(a) << 32U) | b;
}

bool isAlignedBox(const ShapeDef& def, const Pose& pose)
{
    return def.type == ShapeType::Box && pose.rotation.isIdentity();
}

Aabb alignedBox(const ShapeDef& def, const Pose& pose)
{
    return {.min = pose.position + def.polygon.vertices[0],
            .max = pose.position + def.polygon.vertices[2]};
}

Circle worldCircle(const ShapeDef& def, const Pose& pose)
{
    return {.center = pose.apply(def.circle.center), .radius = def.circle.radius};
}

Manifold flipped(Manifold manifold)
{
    manifold.normal = -manifold.normal;
    return manifold;
}

// The narrow phase: picks the collision function for the two shape types.
Manifold collide(const ShapeDef& a, const Pose& poseA, const ShapeDef& b, const Pose& poseB,
                 float margin)
{
    const bool circleA = a.type == ShapeType::Circle;
    const bool circleB = b.type == ShapeType::Circle;
    if (circleA && circleB) {
        return collideCircles(worldCircle(a, poseA), worldCircle(b, poseB), margin);
    }
    const bool boxA = isAlignedBox(a, poseA);
    const bool boxB = isAlignedBox(b, poseB);
    if (boxA && boxB) {
        return collideAabbs(alignedBox(a, poseA), alignedBox(b, poseB), margin, a.edgeMask,
                            b.edgeMask);
    }
    if (boxA && circleB) {
        return collideAabbCircle(alignedBox(a, poseA), worldCircle(b, poseB), margin, a.edgeMask);
    }
    if (circleA && boxB) {
        return flipped(
            collideAabbCircle(alignedBox(b, poseB), worldCircle(a, poseA), margin, b.edgeMask));
    }
    if (circleB) {
        return collidePolygonCircle(a.polygon, poseA, b.circle, poseB, margin, a.edgeMask);
    }
    if (circleA) {
        return flipped(collidePolygonCircle(b.polygon, poseB, a.circle, poseA, margin, b.edgeMask));
    }
    return collidePolygons(a.polygon, poseA, b.polygon, poseB, margin, a.edgeMask, b.edgeMask);
}

// Union-find over body indices, for islands of touching bodies.
std::uint32_t findRoot(std::vector<std::uint32_t>& parent, std::uint32_t i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

} // namespace

void PhysicsWorld::Impl::step(float dt)
{
    const auto stepStart = Clock::now();
    beginEvents.clear();
    endEvents.clear();
    triggerBegin.clear();
    triggerEnd.clear();
    endEvents.swap(pendingEnd);
    triggerEnd.swap(pendingTriggerEnd);
    flushTouchWakes();

    const auto broadStart = Clock::now();
    findPairs(dt);
    stats.broadPhaseMs = millisecondsSince(broadStart);

    const auto narrowStart = Clock::now();
    updateContacts(dt);
    stats.narrowPhaseMs = millisecondsSince(narrowStart);

    const auto solverStart = Clock::now();
    buildIslands();
    solve(dt);
    updateSleep(dt);
    stats.solverMs = millisecondsSince(solverStart);

    for (Body& b : bodies) {
        b.force = glm::vec2(0.0F);
        b.torque = 0.0F;
    }
    stats.bodies = static_cast<int>(bodyCount);
    stats.shapes = static_cast<int>(shapeCount);
    stats.stepMs = millisecondsSince(stepStart);
}

void PhysicsWorld::Impl::findPairs(float dt)
{
    const float margin = settings.speculativeDistance * scale();
    proxies.clear();
    for (std::uint32_t i = 0; i < shapes.size(); ++i) {
        const ShapeData& s = shapes[i];
        if (!s.alive) {
            continue;
        }
        const Body& b = bodies[s.body];
        Aabb box = shapeBounds(s.def, b.pose()).expanded(margin);
        if (b.bullet && b.isDynamic() && b.awake) {
            // Swept box, so the pair is found before the bullet passes through.
            const glm::vec2 motion = b.velocity * dt;
            box = box.merged({.min = box.min + motion, .max = box.max + motion});
        }
        proxies.push_back({.aabb = box, .id = i, .movable = b.isDynamic()});
    }
    broadPhase.setType(settings.broadPhase);
    broadPhase.setCellSize(settings.gridCellSize);
    broadPhase.findPairs(proxies, pairs);
    stats.pairTests = broadPhase.pairTests();

    std::erase_if(pairs, [&](const ProxyPair& pair) {
        const ShapeData& a = shapes[pair.first];
        const ShapeData& b = shapes[pair.second];
        if (a.body == b.body || (a.def.isTrigger && b.def.isTrigger)) {
            return true;
        }
        if ((a.def.category & b.def.mask) == 0 || (b.def.category & a.def.mask) == 0) {
            return true;
        }
        return !bodies[a.body].isDynamic() && !bodies[b.body].isDynamic();
    });
    // Sorted by key: the contacts come out in the same order whatever broad phase found them,
    // which keeps the simulation deterministic, and they can be merged with the last step's list.
    std::ranges::sort(pairs, [](const ProxyPair& x, const ProxyPair& y) {
        return pairKey(x.first, x.second) < pairKey(y.first, y.second);
    });
    stats.candidatePairs = static_cast<int>(pairs.size());
}

void PhysicsWorld::Impl::updateContacts(float dt)
{
    std::swap(contacts, previousContacts);
    contacts.clear();
    std::size_t old = 0;
    for (const ProxyPair& pair : pairs) {
        const std::uint64_t key = pairKey(pair.first, pair.second);
        while (old < previousContacts.size() && previousContacts[old].key < key) {
            endContact(previousContacts[old++]);
        }
        const bool existed = old < previousContacts.size() && previousContacts[old].key == key;
        Contact contact;
        if (existed) {
            contact = previousContacts[old++];
            contact.isNew = false;
        } else {
            contact.isNew = true;
            contact.shapeA = pair.first;
            contact.shapeB = pair.second;
            contact.key = key;
            contact.trigger = shapes[pair.first].def.isTrigger || shapes[pair.second].def.isTrigger;
        }
        updateContact(contact, existed, dt);
        contacts.push_back(contact);
    }
    while (old < previousContacts.size()) {
        endContact(previousContacts[old++]);
    }
    stats.touchingContacts = static_cast<int>(
        std::ranges::count_if(contacts, [](const Contact& c) { return c.touching && !c.trigger; }));
}

void PhysicsWorld::Impl::endContact(const Contact& contact)
{
    if (!contact.touching) {
        return;
    }
    if (contact.trigger) {
        triggerEnd.push_back(triggerEvent(contact));
    } else {
        endEvents.push_back(endEvent(contact));
    }
}

void PhysicsWorld::Impl::updateContact(Contact& contact, bool existed, float dt)
{
    const Body& bodyA = bodies[shapes[contact.shapeA].body];
    const Body& bodyB = bodies[shapes[contact.shapeB].body];
    // Neither body moves (asleep or static): the manifold from the last step is still right.
    if (existed && !bodyA.active() && !bodyB.active()) {
        return;
    }
    Manifold manifold =
        collide(shapes[contact.shapeA].def, bodyA.pose(), shapes[contact.shapeB].def, bodyB.pose(),
                contactMargin(contact, dt));
    if (contact.trigger) {
        updateTrigger(contact, manifold);
        return;
    }
    if (shapes[contact.shapeA].def.oneWay != shapes[contact.shapeB].def.oneWay) {
        decideOneWay(contact, manifold, dt);
    }
    if (existed) {
        warmStartFrom(contact.manifold, manifold);
    }
    contact.manifold = manifold;
    reportTouching(contact);
}

float PhysicsWorld::Impl::contactMargin(const Contact& contact, float dt) const
{
    if (contact.trigger) {
        return 0.0F; // triggers report real overlaps only
    }
    const Body& bodyA = bodies[shapes[contact.shapeA].body];
    const Body& bodyB = bodies[shapes[contact.shapeB].body];
    float margin = settings.speculativeDistance * scale();
    if (bodyA.bullet || bodyB.bullet) {
        // Continuous collision: contact points as far ahead as the bodies move in this step; the
        // solver lets them close only that gap, so they stop at the surface.
        margin += (glm::length(bodyA.velocity) + glm::length(bodyB.velocity)) * dt;
    }
    return margin;
}

void PhysicsWorld::Impl::updateTrigger(Contact& contact, const Manifold& manifold)
{
    const bool overlapping = manifold.count > 0;
    if (overlapping && !contact.touching) {
        triggerBegin.push_back(triggerEvent(contact));
    } else if (!overlapping && contact.touching) {
        triggerEnd.push_back(triggerEvent(contact));
    }
    contact.touching = overlapping;
    contact.manifold = manifold;
}

void PhysicsWorld::Impl::decideOneWay(Contact& contact, const Manifold& manifold, float dt) const
{
    // One-way platforms: a shape that arrives from the platform's top, not moving up and not
    // already sunk into it, collides; any other is let through. The depth check matters for
    // top-only tiles, whose normal is always up even when the shape enters from the side or below.
    if (manifold.count == 0) {
        contact.oneWay = OneWayState::Undecided;
        return;
    }
    if (contact.oneWay == OneWayState::Enabled) {
        return;
    }
    const auto platformShape = shapes[contact.shapeA].def.oneWay ? contact.shapeA : contact.shapeB;
    const bool platformIsA = platformShape == contact.shapeA;
    const Body& platform = bodies[shapes[platformShape].body];
    const Body& other = bodies[shapes[platformIsA ? contact.shapeB : contact.shapeA].body];
    const glm::vec2 up = platform.rotation.apply({0.0F, 1.0F});
    const glm::vec2 normalFromPlatform = platformIsA ? manifold.normal : -manifold.normal;
    const float rising = glm::dot(other.velocity - platform.velocity, up);
    const bool fromAbove = glm::dot(normalFromPlatform, up) > 0.5F;
    const float slop = settings.linearSlop * scale();
    const float depth = -manifold.minSeparation();
    if (contact.oneWay == OneWayState::Undecided) {
        // A shape falling onto the top sinks in at most as far as it fell in the last step.
        const float maxDepth = slop + (std::max(-rising, 0.0F) * dt);
        contact.oneWay = fromAbove && depth <= maxDepth && rising <= slop / dt
                             ? OneWayState::Enabled
                             : OneWayState::Disabled;
        return;
    }
    // Let through, e.g. on its way up: once it falls again with its bottom in the platform's
    // upper half (a jump that just reached the top), it lands on it instead of falling through.
    const Aabb bounds = shapeBounds(shapes[platformShape].def, platform.pose());
    const float halfThickness = std::abs(glm::dot(bounds.max - bounds.min, up)) * 0.5F;
    if (fromAbove && rising < -slop / dt && depth <= halfThickness) {
        contact.oneWay = OneWayState::Enabled;
    }
}

void PhysicsWorld::Impl::warmStartFrom(const Manifold& before, Manifold& now)
{
    // Points with the same feature id start from last step's impulses.
    for (int i = 0; i < now.count; ++i) {
        ManifoldPoint& point = now.points[static_cast<std::size_t>(i)];
        for (int j = 0; j < before.count; ++j) {
            const ManifoldPoint& old = before.points[static_cast<std::size_t>(j)];
            if (old.id == point.id) {
                point.normalImpulse = old.normalImpulse;
                point.tangentImpulse = old.tangentImpulse;
                break;
            }
        }
    }
}

void PhysicsWorld::Impl::reportTouching(Contact& contact)
{
    const Manifold& manifold = contact.manifold;
    const float slop = settings.linearSlop * scale();
    const bool touching = contact.solid() && manifold.count > 0 && manifold.minSeparation() < slop;
    if (touching && !contact.touching) {
        const std::uint32_t a = shapes[contact.shapeA].body;
        const std::uint32_t b = shapes[contact.shapeB].body;
        const glm::vec2 point = manifold.points[0].point;
        const glm::vec2 velocityA =
            bodies[a].velocity + cross(bodies[a].angularVelocity, point - bodies[a].center);
        const glm::vec2 velocityB =
            bodies[b].velocity + cross(bodies[b].angularVelocity, point - bodies[b].center);
        beginEvents.push_back({.shapeA = shapeId(contact.shapeA),
                               .shapeB = shapeId(contact.shapeB),
                               .bodyA = bodyId(a),
                               .bodyB = bodyId(b),
                               .normal = manifold.normal,
                               .point = point,
                               .approachSpeed = -glm::dot(velocityB - velocityA, manifold.normal)});
    } else if (!touching && contact.touching) {
        endEvents.push_back(endEvent(contact));
    }
    contact.touching = touching;
}

void PhysicsWorld::Impl::buildIslands()
{
    // Bodies that touch (directly or through others) form an island; an island sleeps and wakes
    // as a whole, so a stack does not sleep while a box on top still moves.
    islandParent.resize(bodies.size());
    std::iota(islandParent.begin(), islandParent.end(), 0U);
    // A moving kinematic body keeps what it touches awake.
    const auto moving = [](const Body& k) {
        return k.type == BodyType::Kinematic &&
               (k.velocity != glm::vec2(0.0F) || k.angularVelocity != 0.0F);
    };
    for (const Contact& contact : contacts) {
        if (!contact.solid() || contact.manifold.count == 0) {
            continue;
        }
        const std::uint32_t a = shapes[contact.shapeA].body;
        const std::uint32_t b = shapes[contact.shapeB].body;
        if (bodies[a].isDynamic() && bodies[b].isDynamic()) {
            islandParent[findRoot(islandParent, a)] = findRoot(islandParent, b);
        }
        // So does a new contact, e.g. with a static body moved onto a sleeping one.
        if (moving(bodies[a]) || contact.isNew) {
            wake(bodies[b]);
        }
        if (moving(bodies[b]) || contact.isNew) {
            wake(bodies[a]);
        }
    }
    // Any awake body wakes its whole island.
    islandAwake.assign(bodies.size(), 0);
    for (std::uint32_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].alive && bodies[i].isDynamic() && bodies[i].awake) {
            islandAwake[findRoot(islandParent, i)] = 1;
        }
    }
    for (std::uint32_t i = 0; i < bodies.size(); ++i) {
        Body& b = bodies[i];
        if (b.alive && b.isDynamic() && !b.awake && islandAwake[findRoot(islandParent, i)] != 0) {
            wake(b);
        }
    }
}

void PhysicsWorld::Impl::updateSleep(float dt)
{
    if (!settings.enableSleep) {
        return;
    }
    const float linear = settings.linearSleepTolerance * scale();
    const float angular = settings.angularSleepTolerance;
    islandSleep.assign(bodies.size(), std::numeric_limits<float>::max());
    for (std::uint32_t i = 0; i < bodies.size(); ++i) {
        Body& b = bodies[i];
        if (!b.alive || !b.isDynamic() || !b.awake) {
            continue;
        }
        const bool resting = b.allowSleep && glm::dot(b.velocity, b.velocity) <= linear * linear &&
                             std::abs(b.angularVelocity) <= angular;
        b.sleepTime = resting ? b.sleepTime + dt : 0.0F;
        float& island = islandSleep[findRoot(islandParent, i)];
        island = std::min(island, b.sleepTime);
    }
    for (std::uint32_t i = 0; i < bodies.size(); ++i) {
        Body& b = bodies[i];
        if (b.alive && b.isDynamic() && b.awake &&
            islandSleep[findRoot(islandParent, i)] >= settings.timeToSleep) {
            b.awake = false;
            b.velocity = glm::vec2(0.0F);
            b.angularVelocity = 0.0F;
        }
    }
}

} // namespace astra2d
