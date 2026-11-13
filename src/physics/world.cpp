#include "astra2d/physics/world.hpp"

#include "world_impl.hpp"

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

namespace {

bool axisAligned(const Polygon& polygon)
{
    if (polygon.count != 4) {
        return false;
    }
    const std::array<glm::vec2, 4> expected = {glm::vec2(0.0F, -1.0F), glm::vec2(1.0F, 0.0F),
                                               glm::vec2(0.0F, 1.0F), glm::vec2(-1.0F, 0.0F)};
    return std::equal(expected.begin(), expected.end(), polygon.normals.begin());
}

} // namespace

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
    removeShapes(body);
    Body& b = w.body(body);
    if (b.touchWakeQueued) {
        std::erase(w.touchWakeQueue, body.index);
        b.touchWakeQueued = false;
    }
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
    b.lastMotion = glm::vec2(0.0F); // moved, but not by the simulation
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

void PhysicsWorld::setBullet(BodyId body, bool bullet)
{
    m_impl->body(body).bullet = bullet;
}

bool PhysicsWorld::isAwake(BodyId body) const
{
    m_impl->flushTouchWakes();
    const Body& b = m_impl->body(body);
    return b.type == BodyType::Kinematic || b.awake;
}

void PhysicsWorld::setAwake(BodyId body, bool awake)
{
    m_impl->flushTouchWakes(); // earlier wakes must not undo this call
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
    if (def.oneWay && def.type != ShapeType::Circle) {
        // A platform is only stood on: just its upward-facing edges give a contact normal, so a
        // shape landing on its very end is not pushed off sideways (as for one-way tiles).
        for (int i = 0; i < def.polygon.count; ++i) {
            if (def.polygon.normals[static_cast<std::size_t>(i)].y <= 0.5F) {
                shape.def.edgeMask &= static_cast<std::uint8_t>(~(1U << static_cast<unsigned>(i)));
            }
        }
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
    w.removeContactsOf([&](std::uint32_t s) { return s == shape.index; });
    Body& b = w.bodies[data.body];
    std::erase(b.shapes, shape.index);
    data.alive = false;
    ++data.generation;
    w.freeShapes.push_back(shape.index);
    --w.shapeCount;
    w.updateMass(b);
    Impl::wake(b);
}

void PhysicsWorld::removeShapes(BodyId body)
{
    Impl& w = *m_impl;
    Body& b = w.body(body);
    if (b.shapes.empty()) {
        return;
    }
    w.removeContactsOf([&](std::uint32_t s) { return w.shapes[s].body == body.index; });
    for (const std::uint32_t s : b.shapes) {
        ShapeData& shape = w.shapes[s];
        shape.alive = false;
        ++shape.generation;
        w.freeShapes.push_back(s);
        --w.shapeCount;
    }
    b.shapes.clear();
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
