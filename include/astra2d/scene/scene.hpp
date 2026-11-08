#pragma once

#include "astra2d/ecs/entity.hpp"
#include "astra2d/ecs/registry.hpp"
#include "astra2d/core/signal.hpp"
#include "astra2d/scene/components.hpp"

#include <glm/vec2.hpp>

#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

/// @addtogroup scene
/// @{

namespace astra2d {

class Renderer2D;
class Scene;

/// A system: game logic that runs every step over the entities it is interested in.
/// @code
/// scene.addSystem(SystemPhase::FixedUpdate, "movement", [](Scene& scene, float dt) {
///     scene.registry().view<Transform, RigidBody>().each([dt](Transform& t, RigidBody& b) {
///         t.position += b.velocity * dt;
///     });
/// });
/// @endcode
using System = std::function<void(Scene& scene, float deltaTime)>;

/// When a system runs.
enum class SystemPhase {
    FixedUpdate, ///< Scene::fixedUpdate(): gameplay and physics, at the fixed timestep.
    Update,      ///< Scene::update(): once per frame, e.g. input and AI.
    LateUpdate,  ///< Scene::update(), after Update: animation (built-in), camera follow.
};

/// A world of entities plus the systems that run on them.
///
/// The Scene owns a Registry and runs systems in a **fixed, explicit order**: by phase, then in
/// the order they were added. Built-in parts:
/// - the "animation" system (LateUpdate) advances every Animator and writes its frame to the
///   SpriteRenderer;
/// - render() draws every visible SpriteRenderer at its entity's world transform, sorted by layer;
/// - a parent/child hierarchy (setParent()), in which a child's Transform is relative to its
///   parent and destroying a parent destroys its children.
///
/// Typical use from an Application:
/// @code
/// void onFixedUpdate(float dt) override { m_scene.fixedUpdate(dt); }
/// void onUpdate(float dt) override      { m_scene.update(dt); }
/// void onRender() override
/// {
///     m_renderer.begin(m_camera);
///     m_scene.render(m_renderer);
///     m_renderer.end();
/// }
/// @endcode
class Scene {
public:
    Scene();
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    /// The entities and components.
    [[nodiscard]] Registry& registry() { return m_registry; }
    /// The entities and components.
    [[nodiscard]] const Registry& registry() const { return m_registry; }

    // --- Entities ------------------------------------------------------------------------------

    /// Creates an entity with a Transform at @p position, and a Name if @p name is not empty.
    Entity createEntity(std::string_view name = {}, glm::vec2 position = glm::vec2(0.0F));

    /// Destroys @p entity and all its descendants now. See destroyLater() for use inside a View.
    /// @pre the entity is valid.
    void destroyEntity(Entity entity);

    /// Destroys @p entity and its descendants at the end of the current fixedUpdate() or update()
    /// (or at the next flushDestroyed()). Safe while iterating a View. Entities queued twice, or
    /// destroyed meanwhile, are skipped.
    void destroyLater(Entity entity);

    /// Destroys the entities queued by destroyLater() now.
    void flushDestroyed();

    /// Destroys every entity (emitting the components' onDestroy signals) and empties the
    /// destroyLater() queue. Systems stay.
    void clear();

    /// The first entity whose Name is @p name (lowest index), or the null entity. O(entities).
    [[nodiscard]] Entity find(std::string_view name) const;

    // --- Hierarchy -------------------------------------------------------------------------------

    /// Makes @p child a child of @p parent (appended after its other children), or a root if
    /// @p parent is the null entity. With @p keepWorldTransform the child's Transform is changed so
    /// it stays where it is on screen; otherwise its Transform is kept and becomes relative to the
    /// new parent.
    /// @pre both are valid, and @p parent is neither @p child nor one of its descendants.
    void setParent(Entity child, Entity parent, bool keepWorldTransform = false);

    /// The parent of @p entity, or the null entity.
    [[nodiscard]] Entity parent(Entity entity) const;

    /// The direct children of @p entity, in the order they were attached.
    [[nodiscard]] std::vector<Entity> children(Entity entity) const;

    /// The world transform of @p entity: its Transform combined with those of its ancestors.
    /// O(depth). Entities without a Transform count as the identity. Entities with Interpolated
    /// are blended by @p alpha (see render()); the default 1 gives the latest state.
    [[nodiscard]] Transform worldTransform(Entity entity, float alpha = 1.0F) const;

    /// Draws @p entity at its current Transform until the next fixed step, instead of blending
    /// from where it was. Call it after moving an Interpolated entity instantly.
    void resetInterpolation(Entity entity);

    // --- Systems ---------------------------------------------------------------------------------

    /// Appends @p system to @p phase. Systems of a phase run in the order they were added.
    /// @pre no system called @p name exists.
    void addSystem(SystemPhase phase, std::string name, System system);

    /// Removes the system called @p name (also a built-in one, e.g. to replace "animation").
    /// Returns false if there is none. Must not be called while systems run.
    bool removeSystem(std::string_view name);

    /// Names of the systems of @p phase, in execution order.
    [[nodiscard]] std::vector<std::string> systemNames(SystemPhase phase) const;

    /// Remembers the Transform of every Interpolated entity, runs the FixedUpdate systems, then
    /// destroys the entities queued by destroyLater().
    void fixedUpdate(float fixedDeltaTime);

    /// Runs the Update systems, then the LateUpdate systems, then destroys the queued entities.
    void update(float deltaTime);

    /// Draws every visible SpriteRenderer at its entity's world transform, lower layers first;
    /// within a layer, parents before children. Call between Renderer2D::begin() and end().
    /// Pass Application::interpolationAlpha() as @p alpha to draw Interpolated entities between
    /// their last two fixed steps, so motion is smooth at any frame rate.
    void render(Renderer2D& renderer, float alpha = 1.0F);

private:
    struct RegisteredSystem {
        SystemPhase phase;
        std::string name;
        System run;
    };

    struct DrawItem {
        int layer = 0;
        Transform world;
        // Stable while render() runs: nothing adds or removes components meanwhile.
        const SpriteRenderer* sprite = nullptr;
    };

    void runPhase(SystemPhase phase, float deltaTime);
    void detach(Entity child);
    void onRelationshipDestroyed(Entity entity);
    void collectDrawItems(Entity root, float alpha);
    [[nodiscard]] Transform localTransform(Entity entity, float alpha) const;
    [[nodiscard]] bool isAncestor(Entity ancestor, Entity entity) const;

    Registry m_registry;
    // A deque: systems added while a phase runs do not move the running one (no copy needed).
    std::deque<RegisteredSystem> m_systems;
    int m_runningSystems = 0; // > 0 while a phase runs
    std::vector<Entity> m_destroyQueue;
    struct PendingDraw {
        Entity entity;
        Transform parentWorld;
    };

    // Reused by render(), so drawing a frame does not allocate once they have grown.
    std::vector<DrawItem> m_drawItems;
    std::vector<PendingDraw> m_drawStack;
    std::vector<Entity> m_siblings;
    Connection m_relationshipCleanup;
};

} // namespace astra2d

/// @}
