#pragma once

#include "astra2d/ecs/entity.hpp"
#include "astra2d/core/signal.hpp"
#include "astra2d/physics/world.hpp"
#include "astra2d/scene/components.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

/// @addtogroup scene
/// @{

namespace astra2d {

class Scene;

/// Two entities' colliders started or stopped touching. Passed to PhysicsSystem's signals.
struct CollisionEvent {
    Entity a;                   ///< First entity.
    Entity b;                   ///< Second entity.
    glm::vec2 normal{0.0F};     ///< Unit vector from @ref a to @ref b (zero for an end event).
    glm::vec2 point{0.0F};      ///< A contact point (zero for an end event).
    float approachSpeed = 0.0F; ///< Speed of the impact along the normal (0 for an end event).

    /// The entity that is not @p self (one of the two).
    [[nodiscard]] Entity other(Entity self) const { return self == a ? b : a; }
};

/// An entity entered or left a trigger collider.
struct TriggerContact {
    Entity trigger; ///< The entity with the trigger collider.
    Entity other;   ///< The entity that entered or left.
};

/// The closest hit of PhysicsSystem::rayCast().
struct EntityRayHit {
    Entity entity;          ///< The entity hit.
    glm::vec2 point{0.0F};  ///< Hit point.
    glm::vec2 normal{0.0F}; ///< Surface normal, facing the ray's origin.
    float fraction = 0.0F;  ///< 0 at the origin, 1 at the end of the ray.
};

/// Runs a PhysicsWorld for the entities of a Scene.
///
/// Entities with a Collider, a TilemapCollider or a RigidBody get a body: dynamic or kinematic as
/// the RigidBody says, static without one. Every fixed step the system
/// 1. creates and updates bodies from the components (also after the game changed them: a moved
///    Transform teleports the body, a changed RigidBody::velocity is applied),
/// 2. steps the world,
/// 3. writes position, rotation and velocity of moving bodies back to Transform and RigidBody,
/// 4. emits onCollisionBegin/End and onTriggerEnter/Exit.
///
/// @code
/// astra2d::Scene m_scene;
/// astra2d::PhysicsSystem m_physics{m_scene}; // after the scene: it must be destroyed first
///
/// const auto box = m_scene.createEntity("box", {0.0F, 5.0F});
/// m_scene.registry().emplace<astra2d::RigidBody>(box);
/// m_scene.registry().emplace<astra2d::Collider>(box);
/// m_physics.onCollisionBegin().connect([](const astra2d::CollisionEvent& e) { ... });
/// @endcode
///
/// It adds itself as the FixedUpdate system "physics": systems added before it (e.g. a player
/// controller setting velocities) run before the physics step, systems added after it see the
/// result. Positions are world positions: a child entity's Transform is converted. Collider sizes
/// are not scaled by Transform::scale.
///
/// Signal handlers run after the step, outside any view: they may create entities and destroy
/// them (destroyLater() or destroyEntity()). End events may name entities that are already
/// destroyed; check Registry::valid() before using them.
class PhysicsSystem {
public:
    /// Adds the "physics" system to @p scene. @pre the scene outlives this object.
    explicit PhysicsSystem(Scene& scene, const PhysicsSettings& settings = {});
    /// Removes the system and destroys the bodies.
    ~PhysicsSystem();
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;
    PhysicsSystem(PhysicsSystem&&) = delete;
    PhysicsSystem& operator=(PhysicsSystem&&) = delete;

    /// The simulation: settings, stats, contacts and direct access to bodies (see body()).
    [[nodiscard]] PhysicsWorld& world() { return m_world; }
    /// The simulation.
    [[nodiscard]] const PhysicsWorld& world() const { return m_world; }

    /// Creates and updates bodies from the components now (done at the start of every step), e.g.
    /// before a ray cast in the same frame the colliders were added.
    void sync();

    /// sync(), PhysicsWorld::step(), write-back and events; what the "physics" system runs.
    void step(float deltaTime);

    /// The body of @p entity, or a null id if it has none (yet).
    [[nodiscard]] BodyId body(Entity entity) const;
    /// The entity of @p body, or the null entity.
    [[nodiscard]] Entity entity(BodyId body) const;

    /// True if @p entity stands on something (see PhysicsWorld::isGrounded()).
    [[nodiscard]] bool isGrounded(Entity entity, glm::vec2 up = {0.0F, 1.0F},
                                  float minDot = 0.7F) const;

    /// The closest entity hit by the ray from @p origin to `origin + translation` (triggers
    /// skipped).
    [[nodiscard]] std::optional<EntityRayHit> rayCast(glm::vec2 origin, glm::vec2 translation,
                                                      std::uint16_t mask = 0xFFFF) const;

    /// Two solid colliders started touching.
    [[nodiscard]] Signal<const CollisionEvent&>& onCollisionBegin() { return m_collisionBegin; }
    /// Two solid colliders stopped touching (or one was removed).
    [[nodiscard]] Signal<const CollisionEvent&>& onCollisionEnd() { return m_collisionEnd; }
    /// An entity started overlapping a trigger.
    [[nodiscard]] Signal<const TriggerContact&>& onTriggerEnter() { return m_triggerEnter; }
    /// An entity stopped overlapping a trigger (or one was removed).
    [[nodiscard]] Signal<const TriggerContact&>& onTriggerExit() { return m_triggerExit; }

private:
    // What the world holds for an entity, to see what the game changed since.
    struct Record {
        BodyId body;
        std::vector<ShapeId> shapes;
        std::optional<Collider> collider;
        std::optional<TilemapCollider> tilemap;
        RigidBody rigidBody;      // as last given to / read from the world
        glm::vec2 position{0.0F}; // world pose as last given to / read from the world
        float rotation = 0.0F;
        std::uint32_t stamp = 0; // sync() pass that last saw it
    };

    void syncEntity(Entity entity);
    void createBody(Entity entity, Record& record, const RigidBody& wanted, const Transform& world);
    [[nodiscard]] bool shapesChanged(Entity entity, const Record& record) const;
    void applyChanges(const Record& record, const RigidBody& wanted);
    void rebuildShapes(Entity entity, Record& record);
    void writeBack();
    void writeBack(Entity entity);
    void dispatchEvents();
    void forget(Entity entity);

    Scene* m_scene;
    PhysicsWorld m_world;
    std::unordered_map<Entity, Record> m_records;
    std::uint32_t m_stamp = 0;
    // Entity of each body, also for one step after the body is destroyed, for its end events.
    std::unordered_map<std::uint64_t, Entity> m_entities;
    std::vector<std::uint64_t> m_retired;
    std::vector<std::pair<int, Entity>> m_children; // writeBack() scratch: (depth, entity)
    Signal<const CollisionEvent&> m_collisionBegin;
    Signal<const CollisionEvent&> m_collisionEnd;
    Signal<const TriggerContact&> m_triggerEnter;
    Signal<const TriggerContact&> m_triggerExit;
    std::vector<Connection> m_connections;
};

} // namespace astra2d

/// @}
