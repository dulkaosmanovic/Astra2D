#pragma once

// PhysicsWorld's internal state, shared by world.cpp (the public API), step.cpp (pairs, contacts,
// islands, sleep) and solver.cpp (the contact solver). Not part of the public API.

#include "astra2d/core/assert.hpp"
#include "astra2d/physics/world.hpp"

#include <glm/geometric.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace astra2d {

namespace detail {

inline constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

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
    bool touchWakeQueued = false; // in Impl::touchWakeQueue
    float sleepTime = 0.0F;
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

inline Aabb shapeBounds(const ShapeDef& def, const Pose& pose)
{
    return def.type == ShapeType::Circle ? computeAabb(def.circle, pose)
                                         : computeAabb(def.polygon, pose);
}

} // namespace detail

using detail::Body;
using detail::Constraint;
using detail::ConstraintPoint;
using detail::Contact;
using detail::OneWayState;
using detail::ShapeData;
using detail::shapeBounds;

struct PhysicsWorld::Impl {
    PhysicsSettings settings;
    std::vector<Body> bodies;
    std::vector<std::uint32_t> freeBodies;
    std::size_t bodyCount = 0;
    std::vector<ShapeData> shapes;
    std::vector<std::uint32_t> freeShapes;
    std::size_t shapeCount = 0;
    std::vector<std::uint32_t> touchWakeQueue; // bodies whose touching bodies are to be woken

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

    // Wakes the bodies that touch body `index`, e.g. after it moved. Queued so that moving many
    // bodies costs one pass over the contacts (flushTouchWakes), not one pass per body.
    void wakeTouching(std::uint32_t index)
    {
        if (!bodies[index].touchWakeQueued) {
            bodies[index].touchWakeQueued = true;
            touchWakeQueue.push_back(index);
        }
    }

    // Runs the queued wakeTouching calls; before a step and before reading a body's sleep state.
    void flushTouchWakes()
    {
        if (touchWakeQueue.empty()) {
            return;
        }
        for (const Contact& contact : contacts) {
            Body& a = bodies[shapes[contact.shapeA].body];
            Body& b = bodies[shapes[contact.shapeB].body];
            if (a.touchWakeQueued || b.touchWakeQueued) {
                wake(a);
                wake(b);
            }
        }
        for (const std::uint32_t index : touchWakeQueue) {
            bodies[index].touchWakeQueued = false;
        }
        touchWakeQueue.clear();
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

    // Ends the contacts of the shapes for which `removed(shapeIndex)` is true (events in the
    // next step) and wakes their bodies; one pass over the contacts for any number of shapes.
    template <typename Predicate>
    void removeContactsOf(Predicate removed)
    {
        std::erase_if(contacts, [&](const Contact& contact) {
            if (!removed(contact.shapeA) && !removed(contact.shapeB)) {
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

} // namespace astra2d
