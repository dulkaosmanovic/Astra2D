#include "astra2d/physics/world.hpp"

#include "astra2d/core/assert.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <utility>

namespace astra2d {

// ---------------------------------------------------------------------------------------------
// ShapeDef helpers

ShapeDef ShapeDef::makeBox(glm::vec2 halfExtents, glm::vec2 center)
{
    ShapeDef def;
    def.type = ShapeType::Box;
    def.polygon = astra2d::makeBox(halfExtents, center);
    return def;
}

ShapeDef ShapeDef::makeCircle(float radius, glm::vec2 center)
{
    ShapeDef def;
    def.type = ShapeType::Circle;
    def.circle = {.center = center, .radius = radius};
    return def;
}

ShapeDef ShapeDef::makePolygon(std::span<const glm::vec2> points)
{
    ShapeDef def;
    def.type = ShapeType::Polygon;
    def.polygon = astra2d::makePolygon(points);
    return def;
}

// ---------------------------------------------------------------------------------------------
// Internal state

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

struct Body {
    BodyType type = BodyType::Dynamic;
    glm::vec2 origin{0.0F};
    float angle = 0.0F;
    Rotation rotation;
    glm::vec2 localCenter{0.0F};
    glm::vec2 center{0.0F};
    glm::vec2 velocity{0.0F};
    float angularVelocity = 0.0F;
    glm::vec2 force{0.0F};
    float torque = 0.0F;
    float mass = 1.0F; // as set by the game; invMass is 0 unless dynamic
    float invMass = 0.0F;
    float inertia = 0.0F;
    float invInertia = 0.0F;
    float gravityScale = 1.0F;
    float linearDamping = 0.0F;
    float angularDamping = 0.0F;
    bool fixedRotation = false;
    bool bullet = false;
    bool allowSleep = true;
    bool awake = true;
    float sleepTime = 0.0F;
    std::uint64_t userData = 0;
    std::vector<std::uint32_t> shapes;
    std::uint32_t generation = 0;
    bool alive = false;

    // Solver scratch: velocities that only remove overlap and are dropped after the step.
    glm::vec2 pseudoVelocity{0.0F};
    float pseudoAngularVelocity = 0.0F;

    [[nodiscard]] Pose pose() const { return {.position = origin, .rotation = rotation}; }
    [[nodiscard]] bool isDynamic() const { return type == BodyType::Dynamic; }
    // Simulated in this step: moves by itself (awake dynamic, or kinematic).
    [[nodiscard]] bool active() const
    {
        return type == BodyType::Kinematic || (type == BodyType::Dynamic && awake);
    }
};

struct ShapeData {
    ShapeDef def;
    std::uint32_t body = kNone;
    std::uint32_t generation = 0;
    bool alive = false;
};

enum class OneWayState : std::uint8_t { Undecided, Enabled, Disabled };

struct Contact {
    std::uint32_t shapeA = 0;
    std::uint32_t shapeB = 0;
    std::uint64_t key = 0;
    Manifold manifold;
    bool touching = false; // solid and touching, or (for triggers) overlapping
    bool trigger = false;
    bool isNew = false; // created in this step
    OneWayState oneWay = OneWayState::Undecided;

    [[nodiscard]] bool solid() const { return !trigger && oneWay != OneWayState::Disabled; }
};

struct ConstraintPoint {
    glm::vec2 rA{0.0F};
    glm::vec2 rB{0.0F};
    float normalMass = 0.0F;
    float tangentMass = 0.0F;
    float normalImpulse = 0.0F;
    float tangentImpulse = 0.0F;
    float pseudoImpulse = 0.0F;
    float separation = 0.0F;
    float relativeVelocity = 0.0F;
    float maxNormalImpulse = 0.0F;
};

struct Constraint {
    std::uint32_t contact = 0;
    std::uint32_t bodyA = 0;
    std::uint32_t bodyB = 0;
    float invMassA = 0.0F;
    float invInertiaA = 0.0F;
    float invMassB = 0.0F;
    float invInertiaB = 0.0F;
    glm::vec2 normal{0.0F};
    float friction = 0.0F;
    float restitution = 0.0F;
    int count = 0;
    std::array<ConstraintPoint, 2> points{};
    bool bounced = false; // restitution applied in this step
    // Two points: K = [k11 k12; k12 k22] maps normal impulses to normal velocities; with its
    // inverse both points are solved together (block solver), if K is well conditioned.
    bool block = false;
    float k11 = 0.0F;
    float k12 = 0.0F;
    float k22 = 0.0F;
    float inverse11 = 0.0F;
    float inverse12 = 0.0F;
    float inverse22 = 0.0F;
};

// Solves the two normal impulses of a constraint together: the linear complementarity problem
//   vn = K x + b,  x >= 0,  vn >= 0,  x_i vn_i = 0
// for the accumulated impulses x, by trying which points are pushing (both, one, none). Solving
// them one after the other instead lets the order show: a box resting on two points slowly turns,
// and a tall stack leans. `velocity(i)` is the current relative normal velocity at point i,
// `target[i]` the velocity it must reach at least, `apply(i, delta)` applies an impulse change.
template <typename Velocity, typename Apply>
void solveBlock(const Constraint& c, std::array<float, 2>& impulses, std::array<float, 2> target,
                Velocity velocity, Apply apply)
{
    const float a1 = impulses[0];
    const float a2 = impulses[1];
    // Velocities with the current impulses taken out: vn = K x + b.
    const float b1 = velocity(0) - target[0] - ((c.k11 * a1) + (c.k12 * a2));
    const float b2 = velocity(1) - target[1] - ((c.k12 * a1) + (c.k22 * a2));
    const auto set = [&](float x1, float x2) {
        apply(0, x1 - a1);
        apply(1, x2 - a2);
        impulses = {x1, x2};
    };
    // Both points push.
    const float x1 = -((c.inverse11 * b1) + (c.inverse12 * b2));
    const float x2 = -((c.inverse12 * b1) + (c.inverse22 * b2));
    if (x1 >= 0.0F && x2 >= 0.0F) {
        set(x1, x2);
        return;
    }
    // Only the first pushes; the second separates.
    const float only1 = -b1 / c.k11;
    if (only1 >= 0.0F && (c.k12 * only1) + b2 >= 0.0F) {
        set(only1, 0.0F);
        return;
    }
    // Only the second.
    const float only2 = -b2 / c.k22;
    if (only2 >= 0.0F && (c.k12 * only2) + b1 >= 0.0F) {
        set(0.0F, only2);
        return;
    }
    // Neither: both separate.
    if (b1 >= 0.0F && b2 >= 0.0F) {
        set(0.0F, 0.0F);
    }
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

Aabb shapeBounds(const ShapeDef& def, const Pose& pose)
{
    return def.type == ShapeType::Circle ? computeAabb(def.circle, pose)
                                         : computeAabb(def.polygon, pose);
}

bool axisAligned(const Polygon& polygon)
{
    if (polygon.count != 4) {
        return false;
    }
    const std::array<glm::vec2, 4> expected = {glm::vec2(0.0F, -1.0F), glm::vec2(1.0F, 0.0F),
                                               glm::vec2(0.0F, 1.0F), glm::vec2(-1.0F, 0.0F)};
    return std::equal(expected.begin(), expected.end(), polygon.normals.begin());
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

struct PhysicsWorld::Impl {
    PhysicsSettings settings;
    std::vector<Body> bodies;
    std::vector<std::uint32_t> freeBodies;
    std::size_t bodyCount = 0;
    std::vector<ShapeData> shapes;
    std::vector<std::uint32_t> freeShapes;
    std::size_t shapeCount = 0;

    BroadPhase broadPhase;
    std::vector<BroadPhaseProxy> proxies;
    std::vector<ProxyPair> pairs;
    std::vector<Contact> contacts; // sorted by key
    std::vector<Contact> previousContacts;
    std::vector<Constraint> constraints;
    std::vector<std::uint32_t> islandParent;
    std::vector<float> islandSleep;
    std::vector<std::uint8_t> islandAwake;

    std::vector<ContactBeginEvent> beginEvents;
    std::vector<ContactEndEvent> endEvents;
    std::vector<TriggerEvent> triggerBegin;
    std::vector<TriggerEvent> triggerEnd;
    std::vector<ContactEndEvent> pendingEnd; // from shapes removed between steps
    std::vector<TriggerEvent> pendingTriggerEnd;

    PhysicsStats stats;

    // --- Helpers ------------------------------------------------------------------------------

    [[nodiscard]] Body& body(BodyId id)
    {
        ASTRA_ASSERT(validBody(id), "invalid BodyId");
        return bodies[id.index];
    }
    [[nodiscard]] const Body& body(BodyId id) const
    {
        ASTRA_ASSERT(validBody(id), "invalid BodyId");
        return bodies[id.index];
    }
    [[nodiscard]] bool validBody(BodyId id) const
    {
        return id.index < bodies.size() && bodies[id.index].alive &&
               bodies[id.index].generation == id.generation;
    }
    [[nodiscard]] bool validShape(ShapeId id) const
    {
        return id.index < shapes.size() && shapes[id.index].alive &&
               shapes[id.index].generation == id.generation;
    }
    [[nodiscard]] ShapeData& shape(ShapeId id)
    {
        ASTRA_ASSERT(validShape(id), "invalid ShapeId");
        return shapes[id.index];
    }
    [[nodiscard]] const ShapeData& shape(ShapeId id) const
    {
        ASTRA_ASSERT(validShape(id), "invalid ShapeId");
        return shapes[id.index];
    }
    [[nodiscard]] BodyId bodyId(std::uint32_t index) const
    {
        return {.index = index, .generation = bodies[index].generation};
    }
    [[nodiscard]] ShapeId shapeId(std::uint32_t index) const
    {
        return {.index = index, .generation = shapes[index].generation};
    }
    [[nodiscard]] float scale() const { return settings.lengthUnitsPerMeter; }

    static void wake(Body& b)
    {
        if (b.isDynamic()) {
            b.awake = true;
            b.sleepTime = 0.0F;
        }
    }

    // Wakes the bodies that touch body `index`, e.g. after it moved or was removed.
    void wakeTouching(std::uint32_t index)
    {
        for (const Contact& contact : contacts) {
            const std::uint32_t a = shapes[contact.shapeA].body;
            const std::uint32_t b = shapes[contact.shapeB].body;
            if (a == index || b == index) {
                wake(bodies[a]);
                wake(bodies[b]);
            }
        }
    }

    void updateCenter(Body& b) { b.center = b.origin + b.rotation.apply(b.localCenter); }

    // Centre of mass and inertia from the shapes; the mass is the body's own.
    void updateMass(Body& b)
    {
        const glm::vec2 oldCenter = b.center;
        b.invMass = 0.0F;
        b.inertia = 0.0F;
        b.invInertia = 0.0F;
        b.localCenter = glm::vec2(0.0F);
        if (b.isDynamic()) {
            b.invMass = 1.0F / b.mass;
            spreadMass(b);
        }
        updateCenter(b);
        // Keep the velocity of the points of the body when the centre moves.
        b.velocity += cross(b.angularVelocity, b.center - oldCenter);
    }

    // The body's mass spread evenly over the area of its solid shapes: centre and inertia.
    void spreadMass(Body& b) const
    {
        std::vector<MassData> parts;
        float area = 0.0F;
        glm::vec2 weighted(0.0F);
        for (const std::uint32_t s : b.shapes) {
            const ShapeDef& def = shapes[s].def;
            if (!def.isTrigger) {
                parts.push_back(def.type == ShapeType::Circle ? computeMass(def.circle)
                                                              : computeMass(def.polygon));
                area += parts.back().area;
                weighted += parts.back().area * parts.back().center;
            }
        }
        if (area <= 0.0F) {
            return;
        }
        b.localCenter = weighted / area;
        float unitInertia = 0.0F;
        for (const MassData& part : parts) {
            const glm::vec2 offset = part.center - b.localCenter; // parallel axis theorem
            unitInertia += part.inertia + (part.area * glm::dot(offset, offset));
        }
        b.inertia = b.mass * unitInertia / area;
        b.invInertia = (b.fixedRotation || b.inertia <= 0.0F) ? 0.0F : 1.0F / b.inertia;
    }

    // Ends the contacts of shape `index` (events in the next step) and wakes the bodies.
    void removeContactsOf(std::uint32_t index)
    {
        std::erase_if(contacts, [&](const Contact& contact) {
            if (contact.shapeA != index && contact.shapeB != index) {
                return false;
            }
            const std::uint32_t a = shapes[contact.shapeA].body;
            const std::uint32_t b = shapes[contact.shapeB].body;
            wake(bodies[a]);
            wake(bodies[b]);
            if (contact.touching) {
                if (contact.trigger) {
                    pendingTriggerEnd.push_back(triggerEvent(contact));
                } else {
                    pendingEnd.push_back(endEvent(contact));
                }
            }
            return true;
        });
    }

    [[nodiscard]] ContactEndEvent endEvent(const Contact& contact) const
    {
        return {.shapeA = shapeId(contact.shapeA),
                .shapeB = shapeId(contact.shapeB),
                .bodyA = bodyId(shapes[contact.shapeA].body),
                .bodyB = bodyId(shapes[contact.shapeB].body)};
    }

    [[nodiscard]] TriggerEvent triggerEvent(const Contact& contact) const
    {
        const bool aIsTrigger = shapes[contact.shapeA].def.isTrigger;
        const std::uint32_t trigger = aIsTrigger ? contact.shapeA : contact.shapeB;
        const std::uint32_t other = aIsTrigger ? contact.shapeB : contact.shapeA;
        return {.trigger = shapeId(trigger),
                .other = shapeId(other),
                .triggerBody = bodyId(shapes[trigger].body),
                .otherBody = bodyId(shapes[other].body)};
    }

    [[nodiscard]] ContactInfo info(const Contact& contact) const
    {
        return {.shapeA = shapeId(contact.shapeA),
                .shapeB = shapeId(contact.shapeB),
                .bodyA = bodyId(shapes[contact.shapeA].body),
                .bodyB = bodyId(shapes[contact.shapeB].body),
                .manifold = contact.manifold,
                .touching = contact.touching && !contact.trigger};
    }

    // --- Step ---------------------------------------------------------------------------------

    void step(float dt);
    void findPairs(float dt);
    void updateContacts(float dt);
    void updateContact(Contact& contact, bool existed, float dt);
    [[nodiscard]] float contactMargin(const Contact& contact, float dt) const;
    void updateTrigger(Contact& contact, const Manifold& manifold);
    void decideOneWay(Contact& contact, const Manifold& manifold, float dt) const;
    static void warmStartFrom(const Manifold& before, Manifold& now);
    void reportTouching(Contact& contact);
    void endContact(const Contact& contact);
    void buildIslands();

    void solve(float dt);
    void prepareConstraints();
    void preparePoint(const Constraint& constraint, const ManifoldPoint& mp,
                      ConstraintPoint& cp) const;
    static void prepareBlock(Constraint& constraint);
    void integrateForces(float dt);
    void applyImpulse(const Constraint& constraint, const ConstraintPoint& cp, glm::vec2 impulse);
    [[nodiscard]] glm::vec2 relativeVelocity(const Constraint& constraint,
                                             const ConstraintPoint& cp) const;
    void warmStart();
    void solveFriction(Constraint& constraint);
    void solveNormal(Constraint& constraint, float invDt);
    void applyRestitution();
    void storeImpulses();
    void solvePositions(float dt);
    void solvePseudo(Constraint& constraint, float invDt);
    void integratePositions(float dt);
    void updateSleep(float dt);
};

void PhysicsWorld::Impl::step(float dt)
{
    const auto stepStart = Clock::now();
    beginEvents.clear();
    endEvents.clear();
    triggerBegin.clear();
    triggerEnd.clear();
    endEvents.swap(pendingEnd);
    triggerEnd.swap(pendingTriggerEnd);

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
    // One-way platforms decide once per encounter: a shape that arrives from the platform's top,
    // not moving up, collides; any other is let through until they separate.
    if (manifold.count == 0) {
        contact.oneWay = OneWayState::Undecided;
        return;
    }
    if (contact.oneWay != OneWayState::Undecided) {
        return;
    }
    const bool platformIsA = shapes[contact.shapeA].def.oneWay;
    const Body& platform = bodies[shapes[platformIsA ? contact.shapeA : contact.shapeB].body];
    const Body& other = bodies[shapes[platformIsA ? contact.shapeB : contact.shapeA].body];
    const glm::vec2 up = platform.rotation.apply({0.0F, 1.0F});
    const glm::vec2 normalFromPlatform = platformIsA ? manifold.normal : -manifold.normal;
    const float rising = glm::dot(other.velocity - platform.velocity, up);
    const bool fromAbove = glm::dot(normalFromPlatform, up) > 0.5F;
    const float slop = settings.linearSlop * scale();
    contact.oneWay =
        fromAbove && rising <= slop / dt ? OneWayState::Enabled : OneWayState::Disabled;
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

// ---------------------------------------------------------------------------------------------
// Solver

void PhysicsWorld::Impl::solve(float dt)
{
    // Approach velocities are taken before gravity is added: an impact bounces with the speed it
    // arrived with, not with one more step of gravity (which would gain energy at every bounce).
    prepareConstraints();
    integrateForces(dt);
    warmStart();
    // Sequential impulses: solve each point in turn, many times; clamping the accumulated impulse
    // (not each increment) lets later passes correct earlier ones.
    for (int iteration = 0; iteration < settings.velocityIterations; ++iteration) {
        for (Constraint& constraint : constraints) {
            solveFriction(constraint);
            solveNormal(constraint, 1.0F / dt);
        }
    }
    applyRestitution();
    storeImpulses();
    solvePositions(dt);
    integratePositions(dt);
}

void PhysicsWorld::Impl::prepareConstraints()
{
    constraints.clear();
    for (std::uint32_t c = 0; c < contacts.size(); ++c) {
        const Contact& contact = contacts[c];
        if (!contact.solid() || contact.manifold.count == 0) {
            continue;
        }
        const ShapeData& shapeA = shapes[contact.shapeA];
        const ShapeData& shapeB = shapes[contact.shapeB];
        const Body& bodyA = bodies[shapeA.body];
        const Body& bodyB = bodies[shapeB.body];
        if (!(bodyA.isDynamic() && bodyA.awake) && !(bodyB.isDynamic() && bodyB.awake)) {
            continue;
        }
        Constraint constraint;
        constraint.contact = c;
        constraint.bodyA = shapeA.body;
        constraint.bodyB = shapeB.body;
        constraint.invMassA = bodyA.invMass;
        constraint.invInertiaA = bodyA.invInertia;
        constraint.invMassB = bodyB.invMass;
        constraint.invInertiaB = bodyB.invInertia;
        constraint.normal = contact.manifold.normal;
        constraint.friction = std::sqrt(shapeA.def.friction * shapeB.def.friction);
        constraint.restitution = std::max(shapeA.def.restitution, shapeB.def.restitution);
        constraint.count = contact.manifold.count;
        for (int i = 0; i < constraint.count; ++i) {
            const auto k = static_cast<std::size_t>(i);
            preparePoint(constraint, contact.manifold.points[k], constraint.points[k]);
        }
        if (constraint.count == 2) {
            prepareBlock(constraint);
        }
        constraints.push_back(constraint);
    }
}

void PhysicsWorld::Impl::preparePoint(const Constraint& constraint, const ManifoldPoint& mp,
                                      ConstraintPoint& cp) const
{
    const Body& bodyA = bodies[constraint.bodyA];
    const Body& bodyB = bodies[constraint.bodyB];
    const glm::vec2 n = constraint.normal;
    const glm::vec2 t(n.y, -n.x);
    cp.rA = mp.point - bodyA.center;
    cp.rB = mp.point - bodyB.center;
    // Effective mass along a direction: how much an impulse along it changes the relative
    // velocity there, inverted.
    const auto effectiveMass = [&](glm::vec2 direction) {
        const float rA = cross(cp.rA, direction);
        const float rB = cross(cp.rB, direction);
        const float k = constraint.invMassA + constraint.invMassB +
                        (constraint.invInertiaA * rA * rA) + (constraint.invInertiaB * rB * rB);
        return k > 0.0F ? 1.0F / k : 0.0F;
    };
    cp.normalMass = effectiveMass(n);
    cp.tangentMass = effectiveMass(t);
    cp.normalImpulse = mp.normalImpulse;
    cp.tangentImpulse = mp.tangentImpulse;
    cp.separation = mp.separation;
    cp.relativeVelocity = glm::dot(relativeVelocity(constraint, cp), n);
}

void PhysicsWorld::Impl::prepareBlock(Constraint& constraint)
{
    const glm::vec2 n = constraint.normal;
    const ConstraintPoint& p1 = constraint.points[0];
    const ConstraintPoint& p2 = constraint.points[1];
    const float rn1A = cross(p1.rA, n);
    const float rn1B = cross(p1.rB, n);
    const float rn2A = cross(p2.rA, n);
    const float rn2B = cross(p2.rB, n);
    const float mass = constraint.invMassA + constraint.invMassB;
    constraint.k11 =
        mass + (constraint.invInertiaA * rn1A * rn1A) + (constraint.invInertiaB * rn1B * rn1B);
    constraint.k22 =
        mass + (constraint.invInertiaA * rn2A * rn2A) + (constraint.invInertiaB * rn2B * rn2B);
    constraint.k12 =
        mass + (constraint.invInertiaA * rn1A * rn2A) + (constraint.invInertiaB * rn1B * rn2B);
    const float determinant = (constraint.k11 * constraint.k22) - (constraint.k12 * constraint.k12);
    // Nearly parallel rows (points very close together): solve them one by one.
    constexpr float kMaxCondition = 1000.0F;
    if (constraint.k11 * constraint.k11 < kMaxCondition * determinant) {
        constraint.block = true;
        constraint.inverse11 = constraint.k22 / determinant;
        constraint.inverse12 = -constraint.k12 / determinant;
        constraint.inverse22 = constraint.k11 / determinant;
    }
}

void PhysicsWorld::Impl::integrateForces(float dt)
{
    int awake = 0;
    for (Body& b : bodies) {
        if (!b.alive || !b.isDynamic() || !b.awake) {
            continue;
        }
        ++awake;
        b.velocity += dt * ((b.gravityScale * settings.gravity) + (b.invMass * b.force));
        b.angularVelocity += dt * b.invInertia * b.torque;
        b.velocity *= 1.0F / (1.0F + (dt * b.linearDamping));
        b.angularVelocity *= 1.0F / (1.0F + (dt * b.angularDamping));
    }
    stats.awakeBodies = awake;
}

void PhysicsWorld::Impl::applyImpulse(const Constraint& constraint, const ConstraintPoint& cp,
                                      glm::vec2 impulse)
{
    Body& a = bodies[constraint.bodyA];
    Body& b = bodies[constraint.bodyB];
    a.velocity -= constraint.invMassA * impulse;
    a.angularVelocity -= constraint.invInertiaA * cross(cp.rA, impulse);
    b.velocity += constraint.invMassB * impulse;
    b.angularVelocity += constraint.invInertiaB * cross(cp.rB, impulse);
}

glm::vec2 PhysicsWorld::Impl::relativeVelocity(const Constraint& constraint,
                                               const ConstraintPoint& cp) const
{
    const Body& a = bodies[constraint.bodyA];
    const Body& b = bodies[constraint.bodyB];
    return b.velocity + cross(b.angularVelocity, cp.rB) - a.velocity -
           cross(a.angularVelocity, cp.rA);
}

void PhysicsWorld::Impl::warmStart()
{
    // Last step's impulses again: a resting stack starts close to the solution.
    for (const Constraint& constraint : constraints) {
        const glm::vec2 n = constraint.normal;
        const glm::vec2 t(n.y, -n.x);
        for (int i = 0; i < constraint.count; ++i) {
            const ConstraintPoint& cp = constraint.points[static_cast<std::size_t>(i)];
            applyImpulse(constraint, cp, (cp.normalImpulse * n) + (cp.tangentImpulse * t));
        }
    }
}

void PhysicsWorld::Impl::solveFriction(Constraint& constraint)
{
    // Before the normal impulse: friction is limited by it, which the normal pass then refines.
    const glm::vec2 t(constraint.normal.y, -constraint.normal.x);
    for (int i = 0; i < constraint.count; ++i) {
        ConstraintPoint& cp = constraint.points[static_cast<std::size_t>(i)];
        const float vt = glm::dot(relativeVelocity(constraint, cp), t);
        const float maxFriction = constraint.friction * cp.normalImpulse;
        const float newImpulse =
            std::clamp(cp.tangentImpulse - (cp.tangentMass * vt), -maxFriction, maxFriction);
        const float delta = newImpulse - cp.tangentImpulse;
        cp.tangentImpulse = newImpulse;
        applyImpulse(constraint, cp, delta * t);
    }
}

void PhysicsWorld::Impl::solveNormal(Constraint& constraint, float invDt)
{
    // A speculative point (still apart) may close its gap in this step, no more: its normal
    // velocity must reach at least -gap / dt.
    const glm::vec2 n = constraint.normal;
    if (constraint.block) {
        std::array<float, 2> impulses = {constraint.points[0].normalImpulse,
                                         constraint.points[1].normalImpulse};
        const std::array<float, 2> target = {
            -std::max(constraint.points[0].separation, 0.0F) * invDt,
            -std::max(constraint.points[1].separation, 0.0F) * invDt};
        solveBlock(constraint, impulses, target, [&](int i) {
            return glm::dot(
                relativeVelocity(constraint, constraint.points[static_cast<std::size_t>(i)]), n);
        }, [&](int i, float delta) {
            applyImpulse(constraint, constraint.points[static_cast<std::size_t>(i)], delta * n);
        });
        for (std::size_t i = 0; i < 2; ++i) {
            constraint.points[i].normalImpulse = impulses[i];
            constraint.points[i].maxNormalImpulse =
                std::max(constraint.points[i].maxNormalImpulse, impulses[i]);
        }
        return;
    }
    for (int i = 0; i < constraint.count; ++i) {
        ConstraintPoint& cp = constraint.points[static_cast<std::size_t>(i)];
        const float vn = glm::dot(relativeVelocity(constraint, cp), n);
        const float gap = std::max(cp.separation, 0.0F) * invDt;
        const float newImpulse = std::max(cp.normalImpulse - (cp.normalMass * (vn + gap)), 0.0F);
        const float delta = newImpulse - cp.normalImpulse;
        cp.normalImpulse = newImpulse;
        cp.maxNormalImpulse = std::max(cp.maxNormalImpulse, newImpulse);
        applyImpulse(constraint, cp, delta * n);
    }
}

void PhysicsWorld::Impl::applyRestitution()
{
    // Points that were approaching fast bounce back with e times that speed.
    const float threshold = settings.restitutionThreshold * scale();
    for (Constraint& constraint : constraints) {
        if (constraint.restitution == 0.0F) {
            continue;
        }
        for (int i = 0; i < constraint.count; ++i) {
            ConstraintPoint& cp = constraint.points[static_cast<std::size_t>(i)];
            if (cp.relativeVelocity > -threshold || cp.maxNormalImpulse == 0.0F) {
                continue;
            }
            const float vn = glm::dot(relativeVelocity(constraint, cp), constraint.normal);
            const float target = -constraint.restitution * cp.relativeVelocity;
            const float newImpulse =
                std::max(cp.normalImpulse - (cp.normalMass * (vn - target)), 0.0F);
            const float delta = newImpulse - cp.normalImpulse;
            cp.normalImpulse = newImpulse;
            applyImpulse(constraint, cp, delta * constraint.normal);
            constraint.bounced = true;
        }
    }
}

void PhysicsWorld::Impl::storeImpulses()
{
    // Kept for warm starting the next step.
    for (const Constraint& constraint : constraints) {
        Contact& contact = contacts[constraint.contact];
        for (int i = 0; i < constraint.count; ++i) {
            const auto k = static_cast<std::size_t>(i);
            contact.manifold.points[k].normalImpulse = constraint.points[k].normalImpulse;
            contact.manifold.points[k].tangentImpulse = constraint.points[k].tangentImpulse;
        }
    }
}

void PhysicsWorld::Impl::solvePositions(float dt)
{
    // Overlap removal with pseudo velocities (split impulses): they move the bodies apart but are
    // dropped afterwards, so pushing out of an overlap adds no kinetic energy (no bounce).
    for (Body& b : bodies) {
        b.pseudoVelocity = glm::vec2(0.0F);
        b.pseudoAngularVelocity = 0.0F;
    }
    for (int iteration = 0; iteration < settings.positionIterations; ++iteration) {
        for (Constraint& constraint : constraints) {
            // A bouncing contact moves out of the overlap by itself in this step; pushing it out
            // as well would lift the body, i.e. add potential energy at every bounce.
            if (!constraint.bounced) {
                solvePseudo(constraint, 1.0F / dt);
            }
        }
    }
}

void PhysicsWorld::Impl::solvePseudo(Constraint& constraint, float invDt)
{
    Body& a = bodies[constraint.bodyA];
    Body& b = bodies[constraint.bodyB];
    const glm::vec2 n = constraint.normal;
    const float slop = settings.linearSlop * scale();
    const float beta = settings.positionCorrection;
    const auto pseudoVelocity = [&](const ConstraintPoint& cp) {
        return glm::dot(b.pseudoVelocity + cross(b.pseudoAngularVelocity, cp.rB) -
                            a.pseudoVelocity - cross(a.pseudoAngularVelocity, cp.rA),
                        n);
    };
    const auto applyPseudo = [&](const ConstraintPoint& cp, float delta) {
        const glm::vec2 impulse = delta * n;
        a.pseudoVelocity -= constraint.invMassA * impulse;
        a.pseudoAngularVelocity -= constraint.invInertiaA * cross(cp.rA, impulse);
        b.pseudoVelocity += constraint.invMassB * impulse;
        b.pseudoAngularVelocity += constraint.invInertiaB * cross(cp.rB, impulse);
    };
    // The speed that removes the fraction beta of the overlap beyond the slop in this step.
    const auto target = [&](const ConstraintPoint& cp) {
        return beta * invDt * (-cp.separation - slop);
    };
    // Two overlapping points: together, for the same reason as the velocities.
    if (constraint.block && constraint.points[0].separation < -slop &&
        constraint.points[1].separation < -slop) {
        std::array<float, 2> impulses = {constraint.points[0].pseudoImpulse,
                                         constraint.points[1].pseudoImpulse};
        solveBlock(constraint, impulses,
                   {target(constraint.points[0]), target(constraint.points[1])}, [&](int i) {
            return pseudoVelocity(constraint.points[static_cast<std::size_t>(i)]);
        }, [&](int i, float delta) {
            applyPseudo(constraint.points[static_cast<std::size_t>(i)], delta);
        });
        constraint.points[0].pseudoImpulse = impulses[0];
        constraint.points[1].pseudoImpulse = impulses[1];
        return;
    }
    for (int i = 0; i < constraint.count; ++i) {
        ConstraintPoint& cp = constraint.points[static_cast<std::size_t>(i)];
        if (cp.separation >= -slop) {
            continue;
        }
        const float newImpulse =
            std::max(cp.pseudoImpulse + (cp.normalMass * (target(cp) - pseudoVelocity(cp))), 0.0F);
        applyPseudo(cp, newImpulse - cp.pseudoImpulse);
        cp.pseudoImpulse = newImpulse;
    }
}

void PhysicsWorld::Impl::integratePositions(float dt)
{
    for (Body& b : bodies) {
        if (!b.alive || !b.active()) {
            continue;
        }
        b.center += dt * (b.velocity + b.pseudoVelocity);
        b.angle += dt * (b.angularVelocity + b.pseudoAngularVelocity);
        b.rotation = Rotation::fromAngle(b.angle);
        b.origin = b.center - b.rotation.apply(b.localCenter);
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

// ---------------------------------------------------------------------------------------------
// PhysicsWorld

PhysicsWorld::PhysicsWorld(const PhysicsSettings& settings)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->settings = settings;
}

PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

PhysicsSettings& PhysicsWorld::settings()
{
    return m_impl->settings;
}
const PhysicsSettings& PhysicsWorld::settings() const
{
    return m_impl->settings;
}

BodyId PhysicsWorld::createBody(const BodyDef& def)
{
    Impl& w = *m_impl;
    ASTRA_ASSERT(def.type != BodyType::Dynamic || def.mass > 0.0F,
                 "a dynamic body needs a positive mass");
    std::uint32_t index = 0;
    if (!w.freeBodies.empty()) {
        index = w.freeBodies.back();
        w.freeBodies.pop_back();
    } else {
        index = static_cast<std::uint32_t>(w.bodies.size());
        w.bodies.emplace_back();
    }
    Body& b = w.bodies[index];
    const std::uint32_t generation = b.generation;
    b = Body{};
    b.generation = generation;
    b.alive = true;
    b.type = def.type;
    b.origin = def.position;
    b.angle = def.angle;
    b.rotation = Rotation::fromAngle(def.angle);
    b.mass = def.mass;
    b.gravityScale = def.gravityScale;
    b.linearDamping = def.linearDamping;
    b.angularDamping = def.angularDamping;
    b.fixedRotation = def.fixedRotation;
    b.bullet = def.bullet;
    b.allowSleep = def.allowSleep;
    b.awake = def.awake || def.type != BodyType::Dynamic;
    b.userData = def.userData;
    w.updateMass(b);
    if (def.type != BodyType::Static) {
        b.velocity = def.velocity;
        b.angularVelocity = def.angularVelocity;
    }
    ++w.bodyCount;
    return {.index = index, .generation = generation};
}

void PhysicsWorld::destroyBody(BodyId body)
{
    Impl& w = *m_impl;
    Body& b = w.body(body);
    for (const std::uint32_t s : b.shapes) {
        w.removeContactsOf(s);
        ShapeData& shape = w.shapes[s];
        shape.alive = false;
        ++shape.generation;
        w.freeShapes.push_back(s);
        --w.shapeCount;
    }
    b.shapes.clear();
    b.alive = false;
    ++b.generation;
    w.freeBodies.push_back(body.index);
    --w.bodyCount;
}

bool PhysicsWorld::valid(BodyId body) const
{
    return m_impl->validBody(body);
}

std::size_t PhysicsWorld::bodyCount() const
{
    return m_impl->bodyCount;
}

void PhysicsWorld::forEachBody(const std::function<void(BodyId)>& function) const
{
    for (std::uint32_t i = 0; i < m_impl->bodies.size(); ++i) {
        if (m_impl->bodies[i].alive) {
            function(m_impl->bodyId(i));
        }
    }
}

BodyType PhysicsWorld::type(BodyId body) const
{
    return m_impl->body(body).type;
}

void PhysicsWorld::setType(BodyId body, BodyType type)
{
    Body& b = m_impl->body(body);
    if (b.type == type) {
        return;
    }
    m_impl->wakeTouching(body.index);
    b.type = type;
    if (type == BodyType::Static) {
        b.velocity = glm::vec2(0.0F);
        b.angularVelocity = 0.0F;
    }
    b.awake = true;
    b.sleepTime = 0.0F;
    m_impl->updateMass(b);
}

glm::vec2 PhysicsWorld::position(BodyId body) const
{
    return m_impl->body(body).origin;
}
float PhysicsWorld::angle(BodyId body) const
{
    return m_impl->body(body).angle;
}
Pose PhysicsWorld::pose(BodyId body) const
{
    return m_impl->body(body).pose();
}

void PhysicsWorld::setTransform(BodyId body, glm::vec2 position, float angle)
{
    Body& b = m_impl->body(body);
    b.origin = position;
    b.angle = angle;
    b.rotation = Rotation::fromAngle(angle);
    m_impl->updateCenter(b);
    Impl::wake(b);
    m_impl->wakeTouching(body.index);
}

glm::vec2 PhysicsWorld::worldCenter(BodyId body) const
{
    return m_impl->body(body).center;
}
glm::vec2 PhysicsWorld::velocity(BodyId body) const
{
    return m_impl->body(body).velocity;
}

void PhysicsWorld::setVelocity(BodyId body, glm::vec2 velocity)
{
    Body& b = m_impl->body(body);
    if (b.type == BodyType::Static || b.velocity == velocity) {
        return;
    }
    b.velocity = velocity;
    Impl::wake(b);
}

float PhysicsWorld::angularVelocity(BodyId body) const
{
    return m_impl->body(body).angularVelocity;
}

void PhysicsWorld::setAngularVelocity(BodyId body, float angularVelocity)
{
    Body& b = m_impl->body(body);
    if (b.type == BodyType::Static || b.angularVelocity == angularVelocity) {
        return;
    }
    b.angularVelocity = angularVelocity;
    Impl::wake(b);
}

float PhysicsWorld::mass(BodyId body) const
{
    const Body& b = m_impl->body(body);
    return b.isDynamic() ? b.mass : 0.0F;
}

void PhysicsWorld::setMass(BodyId body, float mass)
{
    ASTRA_ASSERT(mass > 0.0F, "mass must be positive");
    Body& b = m_impl->body(body);
    if (b.mass == mass) {
        return;
    }
    b.mass = mass;
    m_impl->updateMass(b);
    Impl::wake(b);
}

float PhysicsWorld::inertia(BodyId body) const
{
    return m_impl->body(body).inertia;
}
float PhysicsWorld::gravityScale(BodyId body) const
{
    return m_impl->body(body).gravityScale;
}

void PhysicsWorld::setGravityScale(BodyId body, float scale)
{
    Body& b = m_impl->body(body);
    if (b.gravityScale != scale) {
        b.gravityScale = scale;
        Impl::wake(b);
    }
}

void PhysicsWorld::setDamping(BodyId body, float linear, float angular)
{
    Body& b = m_impl->body(body);
    b.linearDamping = linear;
    b.angularDamping = angular;
}

bool PhysicsWorld::fixedRotation(BodyId body) const
{
    return m_impl->body(body).fixedRotation;
}

void PhysicsWorld::setFixedRotation(BodyId body, bool fixed)
{
    Body& b = m_impl->body(body);
    if (b.fixedRotation == fixed) {
        return;
    }
    b.fixedRotation = fixed;
    if (fixed) {
        b.angularVelocity = 0.0F;
    }
    m_impl->updateMass(b);
    Impl::wake(b);
}

bool PhysicsWorld::isBullet(BodyId body) const
{
    return m_impl->body(body).bullet;
}
void PhysicsWorld::setBullet(BodyId body, bool bullet)
{
    m_impl->body(body).bullet = bullet;
}

bool PhysicsWorld::isAwake(BodyId body) const
{
    const Body& b = m_impl->body(body);
    return b.type == BodyType::Kinematic || b.awake;
}

void PhysicsWorld::setAwake(BodyId body, bool awake)
{
    Body& b = m_impl->body(body);
    if (!b.isDynamic()) {
        return;
    }
    if (awake) {
        Impl::wake(b);
    } else {
        b.awake = false;
        b.velocity = glm::vec2(0.0F);
        b.angularVelocity = 0.0F;
    }
}

void PhysicsWorld::setAllowSleep(BodyId body, bool allow)
{
    Body& b = m_impl->body(body);
    b.allowSleep = allow;
    if (!allow) {
        Impl::wake(b);
    }
}

std::uint64_t PhysicsWorld::userData(BodyId body) const
{
    return m_impl->body(body).userData;
}
void PhysicsWorld::setUserData(BodyId body, std::uint64_t data)
{
    m_impl->body(body).userData = data;
}

void PhysicsWorld::applyForce(BodyId body, glm::vec2 force, glm::vec2 point)
{
    Body& b = m_impl->body(body);
    if (!b.isDynamic()) {
        return;
    }
    b.force += force;
    b.torque += cross(point - b.center, force);
    Impl::wake(b);
}

void PhysicsWorld::applyForceToCenter(BodyId body, glm::vec2 force)
{
    Body& b = m_impl->body(body);
    if (b.isDynamic()) {
        b.force += force;
        Impl::wake(b);
    }
}

void PhysicsWorld::applyTorque(BodyId body, float torque)
{
    Body& b = m_impl->body(body);
    if (b.isDynamic()) {
        b.torque += torque;
        Impl::wake(b);
    }
}

void PhysicsWorld::applyImpulse(BodyId body, glm::vec2 impulse, glm::vec2 point)
{
    Body& b = m_impl->body(body);
    if (!b.isDynamic()) {
        return;
    }
    b.velocity += b.invMass * impulse;
    b.angularVelocity += b.invInertia * cross(point - b.center, impulse);
    Impl::wake(b);
}

void PhysicsWorld::applyImpulseToCenter(BodyId body, glm::vec2 impulse)
{
    Body& b = m_impl->body(body);
    if (b.isDynamic()) {
        b.velocity += b.invMass * impulse;
        Impl::wake(b);
    }
}

void PhysicsWorld::applyAngularImpulse(BodyId body, float impulse)
{
    Body& b = m_impl->body(body);
    if (b.isDynamic()) {
        b.angularVelocity += b.invInertia * impulse;
        Impl::wake(b);
    }
}

ShapeId PhysicsWorld::addShape(BodyId bodyId, const ShapeDef& def)
{
    Impl& w = *m_impl;
    Body& b = w.body(bodyId);
    ASTRA_ASSERT(def.type == ShapeType::Circle ? def.circle.radius > 0.0F : def.polygon.count >= 3,
                 "shape without area");
    std::uint32_t index = 0;
    if (!w.freeShapes.empty()) {
        index = w.freeShapes.back();
        w.freeShapes.pop_back();
    } else {
        index = static_cast<std::uint32_t>(w.shapes.size());
        w.shapes.emplace_back();
    }
    ShapeData& shape = w.shapes[index];
    shape.def = def;
    if (def.type == ShapeType::Box && !axisAligned(def.polygon)) {
        shape.def.type = ShapeType::Polygon; // a rotated box: no axis-aligned fast path
    }
    shape.body = bodyId.index;
    shape.alive = true;
    b.shapes.push_back(index);
    ++w.shapeCount;
    w.updateMass(b);
    Impl::wake(b);
    return {.index = index, .generation = shape.generation};
}

void PhysicsWorld::removeShape(ShapeId shape)
{
    Impl& w = *m_impl;
    ShapeData& data = w.shape(shape);
    w.removeContactsOf(shape.index);
    Body& b = w.bodies[data.body];
    std::erase(b.shapes, shape.index);
    data.alive = false;
    ++data.generation;
    w.freeShapes.push_back(shape.index);
    --w.shapeCount;
    w.updateMass(b);
    Impl::wake(b);
}

bool PhysicsWorld::valid(ShapeId shape) const
{
    return m_impl->validShape(shape);
}

BodyId PhysicsWorld::body(ShapeId shape) const
{
    return m_impl->bodyId(m_impl->shape(shape).body);
}

std::vector<ShapeId> PhysicsWorld::shapes(BodyId body) const
{
    std::vector<ShapeId> result;
    for (const std::uint32_t s : m_impl->body(body).shapes) {
        result.push_back(m_impl->shapeId(s));
    }
    return result;
}

const ShapeDef& PhysicsWorld::shapeDef(ShapeId shape) const
{
    return m_impl->shape(shape).def;
}

void PhysicsWorld::setMaterial(ShapeId shape, float friction, float restitution)
{
    ShapeDef& def = m_impl->shape(shape).def;
    def.friction = friction;
    def.restitution = restitution;
}

Aabb PhysicsWorld::shapeAabb(ShapeId shape) const
{
    const ShapeData& data = m_impl->shape(shape);
    return shapeBounds(data.def, m_impl->bodies[data.body].pose());
}

void PhysicsWorld::step(float deltaTime)
{
    if (deltaTime > 0.0F) {
        m_impl->step(deltaTime);
    }
}

const PhysicsStats& PhysicsWorld::stats() const
{
    return m_impl->stats;
}

std::span<const ContactBeginEvent> PhysicsWorld::contactBeginEvents() const
{
    return m_impl->beginEvents;
}
std::span<const ContactEndEvent> PhysicsWorld::contactEndEvents() const
{
    return m_impl->endEvents;
}
std::span<const TriggerEvent> PhysicsWorld::triggerBeginEvents() const
{
    return m_impl->triggerBegin;
}
std::span<const TriggerEvent> PhysicsWorld::triggerEndEvents() const
{
    return m_impl->triggerEnd;
}

std::vector<ContactInfo> PhysicsWorld::contacts() const
{
    std::vector<ContactInfo> result;
    result.reserve(m_impl->contacts.size());
    for (const Contact& contact : m_impl->contacts) {
        result.push_back(m_impl->info(contact));
    }
    return result;
}

std::vector<ContactInfo> PhysicsWorld::contacts(BodyId body) const
{
    ASTRA_ASSERT(valid(body), "invalid BodyId");
    std::vector<ContactInfo> result;
    for (const Contact& contact : m_impl->contacts) {
        if (!contact.touching || contact.trigger) {
            continue;
        }
        ContactInfo info = m_impl->info(contact);
        if (info.bodyA == body) {
            result.push_back(info);
        } else if (info.bodyB == body) {
            // Seen from `body`: swap the sides, so the normal points away from it.
            std::swap(info.shapeA, info.shapeB);
            std::swap(info.bodyA, info.bodyB);
            info.manifold.normal = -info.manifold.normal;
            result.push_back(info);
        }
    }
    return result;
}

bool PhysicsWorld::isGrounded(BodyId body, glm::vec2 up, float minDot) const
{
    ASTRA_ASSERT(valid(body), "invalid BodyId");
    return std::ranges::any_of(m_impl->contacts, [&](const Contact& contact) {
        if (!contact.touching || contact.trigger) {
            return false;
        }
        // The normal pointing towards `body`, i.e. from the ground to it.
        const std::uint32_t a = m_impl->shapes[contact.shapeA].body;
        const std::uint32_t b = m_impl->shapes[contact.shapeB].body;
        return (b == body.index && glm::dot(contact.manifold.normal, up) >= minDot) ||
               (a == body.index && glm::dot(-contact.manifold.normal, up) >= minDot);
    });
}

std::optional<WorldRayHit> PhysicsWorld::rayCast(glm::vec2 origin, glm::vec2 translation,
                                                 std::uint16_t mask) const
{
    std::optional<WorldRayHit> closest;
    const RayCastInput ray{.origin = origin, .translation = translation};
    for (std::uint32_t i = 0; i < m_impl->shapes.size(); ++i) {
        const ShapeData& shape = m_impl->shapes[i];
        if (!shape.alive || shape.def.isTrigger || (shape.def.category & mask) == 0) {
            continue;
        }
        const Pose pose = m_impl->bodies[shape.body].pose();
        const auto hit = shape.def.type == ShapeType::Circle
                             ? astra2d::rayCast(ray, shape.def.circle, pose)
                             : astra2d::rayCast(ray, shape.def.polygon, pose);
        if (hit && (!closest || hit->fraction < closest->hit.fraction)) {
            closest = WorldRayHit{
                .shape = m_impl->shapeId(i), .body = m_impl->bodyId(shape.body), .hit = *hit};
        }
    }
    return closest;
}

std::vector<ShapeId> PhysicsWorld::queryAabb(const Aabb& box) const
{
    std::vector<ShapeId> result;
    for (std::uint32_t i = 0; i < m_impl->shapes.size(); ++i) {
        const ShapeData& shape = m_impl->shapes[i];
        if (shape.alive &&
            shapeBounds(shape.def, m_impl->bodies[shape.body].pose()).overlaps(box)) {
            result.push_back(m_impl->shapeId(i));
        }
    }
    return result;
}

std::vector<ShapeId> PhysicsWorld::queryPoint(glm::vec2 point) const
{
    std::vector<ShapeId> result;
    for (std::uint32_t i = 0; i < m_impl->shapes.size(); ++i) {
        const ShapeData& shape = m_impl->shapes[i];
        if (!shape.alive) {
            continue;
        }
        const glm::vec2 local = m_impl->bodies[shape.body].pose().applyInverse(point);
        bool inside = true;
        if (shape.def.type == ShapeType::Circle) {
            const glm::vec2 d = local - shape.def.circle.center;
            inside = glm::dot(d, d) <= shape.def.circle.radius * shape.def.circle.radius;
        } else {
            const Polygon& polygon = shape.def.polygon;
            for (int k = 0; k < polygon.count && inside; ++k) {
                const auto j = static_cast<std::size_t>(k);
                inside = glm::dot(polygon.normals[j], local - polygon.vertices[j]) <= 0.0F;
            }
        }
        if (inside) {
            result.push_back(m_impl->shapeId(i));
        }
    }
    return result;
}

} // namespace astra2d
