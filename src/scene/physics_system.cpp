#include "astra2d/scene/physics_system.hpp"

#include "astra2d/core/log.hpp"
#include "astra2d/core/profiler.hpp"
#include "astra2d/physics/tilemap.hpp"
#include "astra2d/scene/scene.hpp"

#include <algorithm>
#include <stdexcept>

namespace astra2d {

namespace {

std::uint64_t bodyKey(BodyId body)
{
    return (static_cast<std::uint64_t>(body.index) << 32U) | body.generation;
}

// The world's shape for a Collider, or none if its size or polygon points are unusable.
std::optional<ShapeDef> toShapeDef(const Collider& collider)
{
    ShapeDef def;
    switch (collider.shape) {
    case ColliderShape::Box:
        // Written so that NaN fails too.
        if (!(collider.halfExtents.x > 0.0F) || !(collider.halfExtents.y > 0.0F)) {
            ASTRA_CORE_ERROR("Collider box ignored: half extents ({}, {}) must be positive",
                             collider.halfExtents.x, collider.halfExtents.y);
            return std::nullopt;
        }
        def = ShapeDef::makeBox(collider.halfExtents, collider.offset);
        break;
    case ColliderShape::Circle:
        if (!(collider.radius > 0.0F)) {
            ASTRA_CORE_ERROR("Collider circle ignored: radius {} must be positive",
                             collider.radius);
            return std::nullopt;
        }
        def = ShapeDef::makeCircle(collider.radius, collider.offset);
        break;
    case ColliderShape::Polygon: {
        std::vector<glm::vec2> points = collider.points;
        for (glm::vec2& p : points) {
            p += collider.offset;
        }
        try {
            def = ShapeDef::makePolygon(points);
        } catch (const std::invalid_argument& error) {
            ASTRA_CORE_ERROR("Collider polygon ignored: {}", error.what());
            return std::nullopt;
        }
        break;
    }
    }
    def.friction = collider.friction;
    def.restitution = collider.restitution;
    def.isTrigger = collider.isTrigger;
    def.oneWay = collider.oneWay;
    def.category = collider.category;
    def.mask = collider.mask;
    return def;
}

} // namespace

PhysicsSystem::PhysicsSystem(Scene& scene, const PhysicsSettings& settings)
    : m_scene(&scene)
    , m_world(settings)
{
    Registry& registry = scene.registry();
    // Removing a collider or rigid body (or destroying the entity) drops the body; if the entity
    // keeps one of the others, the next sync() makes a new body from them.
    const auto forgetHandler = [this](Registry&, Entity entity) { forget(entity); };
    m_connections.push_back(registry.onDestroy<Collider>().connect(forgetHandler));
    m_connections.push_back(registry.onDestroy<TilemapCollider>().connect(forgetHandler));
    m_connections.push_back(registry.onDestroy<RigidBody>().connect(forgetHandler));
    scene.addSystem(SystemPhase::FixedUpdate, "physics",
                    [this](Scene&, float deltaTime) { step(deltaTime); });
}

PhysicsSystem::~PhysicsSystem()
{
    m_scene->removeSystem("physics");
}

void PhysicsSystem::forget(Entity entity)
{
    const auto it = m_records.find(entity);
    if (it == m_records.end()) {
        return;
    }
    if (m_world.valid(it->second.body)) {
        m_retired.push_back(bodyKey(it->second.body));
        m_world.destroyBody(it->second.body);
    }
    m_records.erase(it);
}

BodyId PhysicsSystem::body(Entity entity) const
{
    const auto it = m_records.find(entity);
    return it != m_records.end() ? it->second.body : BodyId{};
}

Entity PhysicsSystem::entity(BodyId body) const
{
    const auto it = m_entities.find(bodyKey(body));
    return it != m_entities.end() ? it->second : kNullEntity;
}

void PhysicsSystem::sync()
{
    // Views have a fixed order, so bodies are created and updated deterministically. An entity in
    // more than one view is handled once per pass (stamp).
    ++m_stamp;
    Registry& registry = m_scene->registry();
    for (const Entity e : registry.storage<Collider>().entities()) {
        syncEntity(e);
    }
    for (const Entity e : registry.storage<TilemapCollider>().entities()) {
        syncEntity(e);
    }
    for (const Entity e : registry.storage<RigidBody>().entities()) {
        syncEntity(e);
    }
}

void PhysicsSystem::syncEntity(Entity entity)
{
    Record& record = m_records[entity];
    if (record.stamp == m_stamp) {
        return;
    }
    record.stamp = m_stamp;

    const auto* rigidBody = m_scene->registry().tryGet<RigidBody>(entity);
    const RigidBody wanted =
        rigidBody != nullptr ? *rigidBody : RigidBody{.type = BodyType::Static};
    const Transform world = m_scene->worldTransform(entity);
    if (!m_world.valid(record.body)) {
        createBody(entity, record, wanted, world);
    } else {
        if (shapesChanged(entity, record)) {
            rebuildShapes(entity, record);
        }
        applyChanges(record, wanted);
        if (world.position != record.position || world.rotation != record.rotation) {
            m_world.setTransform(record.body, world.position, world.rotation); // moved by the game
        }
    }
    record.rigidBody = wanted;
    record.position = world.position;
    record.rotation = world.rotation;
}

void PhysicsSystem::createBody(Entity entity, Record& record, const RigidBody& wanted,
                               const Transform& world)
{
    // Checked for every type: a static or kinematic body may become dynamic later. Written so that
    // NaN fails too; release builds have no assertion in createBody() to catch it.
    float mass = wanted.mass;
    if (!(mass > 0.0F)) {
        ASTRA_CORE_ERROR("RigidBody mass {} must be positive; using 1", mass);
        mass = 1.0F;
    }
    record.body = m_world.createBody({.type = wanted.type,
                                      .position = world.position,
                                      .angle = world.rotation,
                                      .velocity = wanted.velocity,
                                      .angularVelocity = wanted.angularVelocity,
                                      .mass = mass,
                                      .gravityScale = wanted.gravityScale,
                                      .linearDamping = wanted.linearDamping,
                                      .angularDamping = wanted.angularDamping,
                                      .fixedRotation = wanted.fixedRotation,
                                      .bullet = wanted.bullet,
                                      .allowSleep = wanted.allowSleep});
    m_entities[bodyKey(record.body)] = entity;
    record.shapes.clear();
    rebuildShapes(entity, record);
}

bool PhysicsSystem::shapesChanged(Entity entity, const Record& record) const
{
    const Registry& registry = m_scene->registry();
    const auto* collider = registry.tryGet<Collider>(entity);
    const auto* tilemap = registry.tryGet<TilemapCollider>(entity);
    // Compared by value: the game may have changed any field of the components.
    const auto differs = [](const auto* now, const auto& before) {
        return now == nullptr ? before.has_value() : !before.has_value() || *now != before.value();
    };
    return differs(collider, record.collider) || differs(tilemap, record.tilemap);
}

// Applies what the game changed in the RigidBody since the last step.
void PhysicsSystem::applyChanges(const Record& record, const RigidBody& wanted)
{
    const RigidBody& before = record.rigidBody;
    const BodyId body = record.body;
    if (wanted.type != before.type) {
        m_world.setType(body, wanted.type);
    }
    if (wanted.mass != before.mass && wanted.mass > 0.0F) {
        m_world.setMass(body, wanted.mass);
    }
    if (wanted.gravityScale != before.gravityScale) {
        m_world.setGravityScale(body, wanted.gravityScale);
    }
    if (wanted.linearDamping != before.linearDamping ||
        wanted.angularDamping != before.angularDamping) {
        m_world.setDamping(body, wanted.linearDamping, wanted.angularDamping);
    }
    if (wanted.fixedRotation != before.fixedRotation) {
        m_world.setFixedRotation(body, wanted.fixedRotation);
    }
    if (wanted.bullet != before.bullet) {
        m_world.setBullet(body, wanted.bullet);
    }
    if (wanted.allowSleep != before.allowSleep) {
        m_world.setAllowSleep(body, wanted.allowSleep);
    }
    if (wanted.velocity != before.velocity) {
        m_world.setVelocity(body, wanted.velocity);
    }
    if (wanted.angularVelocity != before.angularVelocity) {
        m_world.setAngularVelocity(body, wanted.angularVelocity);
    }
}

void PhysicsSystem::rebuildShapes(Entity entity, Record& record)
{
    // All shapes of the body are the record's, so they go in one pass over the contacts.
    m_world.removeShapes(record.body);
    record.shapes.clear();

    const Registry& registry = m_scene->registry();
    const auto* collider = registry.tryGet<Collider>(entity);
    const auto* tilemap = registry.tryGet<TilemapCollider>(entity);
    record.collider = collider != nullptr ? std::optional(*collider) : std::nullopt;
    record.tilemap = tilemap != nullptr ? std::optional(*tilemap) : std::nullopt;
    if (collider != nullptr) {
        if (const auto def = toShapeDef(*collider)) {
            record.shapes.push_back(m_world.addShape(record.body, *def));
        }
    }
    if (tilemap != nullptr && !(tilemap->grid.tileSize > 0.0F)) {
        ASTRA_CORE_ERROR("TilemapCollider ignored: tileSize {} must be positive",
                         tilemap->grid.tileSize);
    } else if (tilemap != nullptr) {
        ShapeDef material;
        material.friction = tilemap->friction;
        material.restitution = tilemap->restitution;
        material.category = tilemap->category;
        material.mask = tilemap->mask;
        const auto shapes = addTileShapes(m_world, record.body, tilemap->grid, material);
        record.shapes.insert(record.shapes.end(), shapes.begin(), shapes.end());
    }
}

void PhysicsSystem::step(float deltaTime)
{
    // Bodies destroyed until now get their end events in this step; after it, their entities
    // need not be known anymore. Those destroyed by the handlers below wait for the next step.
    std::vector<std::uint64_t> retired;
    retired.swap(m_retired);
    {
        ASTRA_PROFILE_SCOPE("sync");
        sync();
    }
    {
        ASTRA_PROFILE_SCOPE("world step");
        m_world.step(deltaTime);
    }
    {
        ASTRA_PROFILE_SCOPE("write back");
        writeBack();
    }
    {
        ASTRA_PROFILE_SCOPE("events"); // the game's collision handlers
        dispatchEvents();
    }
    for (const std::uint64_t key : retired) {
        m_entities.erase(key);
    }
}

void PhysicsSystem::writeBack()
{
    // A child body's Transform is relative to its parent, so it is written after the parent's
    // (the parent may be a body too): roots first, then children by depth in the hierarchy.
    m_children.clear();
    Registry& registry = m_scene->registry();
    registry.view<RigidBody, Transform>().each([&](Entity entity, RigidBody&, Transform&) {
        if (m_scene->parent(entity).isNull()) {
            writeBack(entity);
        } else {
            int depth = 0;
            for (Entity e = entity; !e.isNull(); e = m_scene->parent(e)) {
                ++depth;
            }
            m_children.emplace_back(depth, entity);
        }
    });
    // Stable: same order as the view at equal depth, so writing back stays deterministic.
    std::ranges::stable_sort(m_children, {}, &std::pair<int, Entity>::first);
    for (const auto& [depth, entity] : m_children) {
        writeBack(entity);
    }
}

void PhysicsSystem::writeBack(Entity entity)
{
    const auto it = m_records.find(entity);
    if (it == m_records.end() || !m_world.valid(it->second.body)) {
        return;
    }
    Record& record = it->second;
    const BodyId body = record.body;
    if (m_world.type(body) == BodyType::Static) {
        return;
    }
    Registry& registry = m_scene->registry();
    auto& rigidBody = registry.get<RigidBody>(entity);
    auto& transform = registry.get<Transform>(entity);
    const glm::vec2 position = m_world.position(body);
    const float rotation = m_world.angle(body);
    const Entity parent = m_scene->parent(entity);
    if (parent.isNull()) {
        transform.position = position;
        transform.rotation = rotation;
        record.position = position;
        record.rotation = rotation;
    } else {
        // A child: its Transform is relative to the parent.
        const Transform parentWorld = m_scene->worldTransform(parent);
        const Transform local =
            relativeTo(parentWorld, {.position = position,
                                     .rotation = rotation,
                                     .scale = parentWorld.scale * transform.scale});
        transform.position = local.position;
        transform.rotation = local.rotation;
        // Compared with the recomputed world transform at the next sync, so rounding in the
        // conversion does not look like a teleport.
        const Transform recomputed = m_scene->worldTransform(entity);
        record.position = recomputed.position;
        record.rotation = recomputed.rotation;
    }
    rigidBody.velocity = m_world.velocity(body);
    rigidBody.angularVelocity = m_world.angularVelocity(body);
    record.rigidBody.velocity = rigidBody.velocity;
    record.rigidBody.angularVelocity = rigidBody.angularVelocity;
}

void PhysicsSystem::dispatchEvents()
{
    const Registry& registry = m_scene->registry();
    // A handler may destroy entities and so bodies, but the world's event lists only change in
    // the next step, so they can be read while handlers run.
    for (const ContactBeginEvent& event : m_world.contactBeginEvents()) {
        const CollisionEvent collision{.a = entity(event.bodyA),
                                       .b = entity(event.bodyB),
                                       .normal = event.normal,
                                       .point = event.point,
                                       .approachSpeed = event.approachSpeed};
        // Skip pairs whose entity an earlier handler destroyed.
        if (registry.valid(collision.a) && registry.valid(collision.b)) {
            m_collisionBegin.emit(collision);
        }
    }
    for (const ContactEndEvent& event : m_world.contactEndEvents()) {
        m_collisionEnd.emit({.a = entity(event.bodyA), .b = entity(event.bodyB)});
    }
    for (const TriggerEvent& event : m_world.triggerBeginEvents()) {
        const TriggerContact contact{.trigger = entity(event.triggerBody),
                                     .other = entity(event.otherBody)};
        if (registry.valid(contact.trigger) && registry.valid(contact.other)) {
            m_triggerEnter.emit(contact);
        }
    }
    for (const TriggerEvent& event : m_world.triggerEndEvents()) {
        m_triggerExit.emit(
            {.trigger = entity(event.triggerBody), .other = entity(event.otherBody)});
    }
}

bool PhysicsSystem::isGrounded(Entity entity, glm::vec2 up, float minDot) const
{
    const BodyId id = body(entity);
    return m_world.valid(id) && m_world.isGrounded(id, up, minDot);
}

std::optional<EntityRayHit> PhysicsSystem::rayCast(glm::vec2 origin, glm::vec2 translation,
                                                   std::uint16_t mask) const
{
    const auto hit = m_world.rayCast(origin, translation, mask);
    if (!hit) {
        return std::nullopt;
    }
    return EntityRayHit{.entity = entity(hit->body),
                        .point = hit->hit.point,
                        .normal = hit->hit.normal,
                        .fraction = hit->hit.fraction};
}

} // namespace astra2d
