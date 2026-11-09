#pragma once

#include "astra2d/ecs/entity.hpp"
#include "astra2d/physics/tilemap.hpp"
#include "astra2d/physics/world.hpp"
#include "astra2d/renderer/animation.hpp"
#include "astra2d/renderer/sprite.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// @addtogroup scene
/// @{

namespace astra2d {

/// A readable name, e.g. for Scene::find(), debugging and scene files. Optional.
struct Name {
    std::string value; ///< The name.
};

/// Position, rotation and scale of an entity, relative to its parent (see Relationship), or to
/// the world if it has none. Scene::createEntity() adds one to every entity.
struct Transform {
    glm::vec2 position{0.0F}; ///< Position in world units.
    float rotation = 0.0F;    ///< Counter-clockwise rotation in radians.
    glm::vec2 scale{1.0F};    ///< Scale factors; multiply SpriteRenderer::size and child offsets.
};

/// Combines a @p parent world transform with a @p local transform of a child into the child's world
/// transform: the child's position is scaled, rotated and moved with the parent, rotations add and
/// scales multiply. (A non-uniformly scaled parent with a rotated child would need a shear, which a
/// Transform cannot express; the result then keeps the child's own axes.)
[[nodiscard]] Transform compose(const Transform& parent, const Transform& local);

/// The inverse of compose(): the local transform that gives @p world under @p parent.
/// @pre parent.scale has no zero component.
[[nodiscard]] Transform relativeTo(const Transform& parent, const Transform& world);

/// Smooth drawing for an entity that moves at the fixed timestep.
///
/// Game logic runs at a fixed rate (e.g. 60 steps per second) but frames come at the display's
/// rate, so an entity drawn at its latest Transform moves in visible jumps on a 144 Hz display.
/// With this component, Scene::fixedUpdate() remembers the Transform from before each step, and
/// Scene::render() / Scene::worldTransform() blend between that and the current one by
/// Application::interpolationAlpha(). Rotation is blended linearly, so keep it continuous (do not
/// wrap it from 2π to 0 in one step).
///
/// After moving an entity instantly (respawn, teleport), call Scene::resetInterpolation(), or it is
/// drawn sliding across the screen for one step.
struct Interpolated {
    Transform previous; ///< Transform before the last fixed step; kept by the Scene.
    bool valid = false; ///< False until the next fixed step: the current Transform is drawn.
};

/// Blends two transforms: @p alpha 0 gives @p from, 1 gives @p to.
[[nodiscard]] Transform interpolate(const Transform& from, const Transform& to, float alpha);

/// Draws a sprite at the entity's world transform. Drawn by Scene::render().
struct SpriteRenderer {
    Sprite sprite{};      ///< Texture and region; a null texture draws a solid quad.
    glm::vec2 size{1.0F}; ///< Size in world units before Transform::scale.
    glm::vec4 tint{1.0F}; ///< Colour multiplied with the texture (the colour without one).
    int layer = 0;        ///< Higher layers are drawn on top of lower ones.
    bool flipX = false;   ///< Mirror horizontally (e.g. a character facing left).
    bool visible = true;  ///< Skip drawing while false.
};

/// Plays a sprite sheet animation into the entity's SpriteRenderer. Advanced by Scene::update().
///
/// @code
/// auto& animator = registry.emplace<Animator>(player, Animator{.sheet = &m_sheet});
/// animator.player.play(m_runAnimation); // the animation must outlive the component
/// @endcode
struct Animator {
    const SpriteSheet* sheet = nullptr; ///< Frames come from this sheet; must outlive the entity.
    AnimationPlayer player{};           ///< Which animation, current frame, speed.
    /// Destroy the entity when its animation has finished (a one-shot effect such as dust or a
    /// sparkle); has no effect on a looping animation.
    bool destroyWhenFinished = false;
};

/// Destroys its entity after @ref duration seconds, so short-lived things (a burst, a squashed
/// enemy, a bullet) need no system of their own. The built-in "lifetime" system (LateUpdate, so
/// the game's systems see the last moment) advances @ref age by the frame time.
///
/// @code
/// registry.emplace<Lifetime>(burst, Lifetime{.duration = 0.4F});
/// sprite.tint.a = 1.0F - registry.get<Lifetime>(burst).progress(); // a fade
/// @endcode
struct Lifetime {
    float duration = 1.0F; ///< Seconds the entity lives.
    float age = 0.0F;      ///< Seconds lived so far; destroyed once it reaches duration.
    /// age / duration in [0, 1] (1 for a duration of 0 or less).
    [[nodiscard]] float progress() const
    {
        return duration > 0.0F ? std::clamp(age / duration, 0.0F, 1.0F) : 1.0F;
    }
    /// Equal if all fields are equal.
    friend bool operator==(const Lifetime&, const Lifetime&) = default;
};

/// Physical motion of an entity, simulated by PhysicsSystem; it writes the result to Transform.
///
/// The game may change any field: the system applies it at the next step. Setting @ref velocity
/// directly suits characters (`body.velocity.x = speed`); for pushes use
/// PhysicsSystem::world() and applyImpulse(). An entity with a RigidBody but no Collider is a point
/// mass without collisions.
struct RigidBody {
    BodyType type = BodyType::Dynamic; ///< Static, dynamic or kinematic.
    glm::vec2 velocity{0.0F};          ///< World units per second.
    float mass = 1.0F;                 ///< Mass of a dynamic body (> 0).
    float gravityScale = 1.0F;         ///< Multiplies the world's gravity (0 = floats).
    float linearDamping = 0.0F;        ///< Fraction of velocity lost per second (air drag).
    float angularVelocity = 0.0F;      ///< Radians per second, counter-clockwise.
    float angularDamping = 0.0F;       ///< Fraction of angular velocity lost per second.
    bool fixedRotation = false;        ///< Never rotates (e.g. a platformer character).
    bool bullet = false;               ///< Continuous collision for fast projectiles.
    bool allowSleep = true;            ///< May fall asleep when at rest.
    /// Equal if all fields are equal.
    friend bool operator==(const RigidBody&, const RigidBody&) = default;
};

/// The shape of a Collider.
enum class ColliderShape {
    Box,     ///< Box with Collider::halfExtents, rotated with the entity.
    Circle,  ///< Circle with Collider::radius.
    Polygon, ///< Convex hull of Collider::points (3 to 8 corners).
};

/// The collision shape of an entity, centred on its position plus @p offset (rotated with it).
/// Sizes are in world units; Transform::scale does not change them. Without a RigidBody the
/// entity is a static collider (a wall). Changes are applied by PhysicsSystem at the next step.
struct Collider {
    ColliderShape shape = ColliderShape::Box; ///< Box, circle or polygon.
    glm::vec2 halfExtents{0.5F};              ///< Half width and height of a box.
    float radius = 0.5F;                      ///< Radius of a circle.
    glm::vec2 offset{0.0F};                   ///< Centre relative to the entity's position.
    float restitution = 0.0F;                 ///< Bounciness: 0 = no bounce, 1 = elastic.
    float friction = 0.5F;                    ///< Friction coefficient.
    bool isTrigger = false;                   ///< Reports overlaps but does not collide.
    /// Corners of a polygon, relative to offset.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Wmissing-field-initializers
    std::vector<glm::vec2> points{};
    bool oneWay = false;             ///< Collides only from above (one-way platform).
    std::uint16_t category = 0x0001; ///< Collision groups this collider is in (bits).
    std::uint16_t mask = 0xFFFF;     ///< Groups it collides with.
    /// Equal if all fields are equal.
    friend bool operator==(const Collider&, const Collider&) = default;
};

/// Static collision for a tile map: the solid and one-way tiles of @ref grid become a few box
/// shapes (see mergeTiles()), placed relative to the entity's position. Handled by PhysicsSystem.
struct TilemapCollider {
    TileGrid grid{};          ///< Which tiles collide; TileGrid::origin is relative to the entity.
    float friction = 0.5F;    ///< Friction of every tile.
    float restitution = 0.0F; ///< Bounciness of every tile.
    std::uint16_t category = 0x0001; ///< Collision groups of the tiles.
    std::uint16_t mask = 0xFFFF;     ///< Groups the tiles collide with.
    /// Equal if all fields are equal.
    friend bool operator==(const TilemapCollider&, const TilemapCollider&) = default;
};

/// Links an entity into the scene hierarchy. Managed by Scene::setParent(); do not change it
/// directly. Children form a doubly linked list, so attaching and detaching is O(1) apart from
/// finding the last child.
struct Relationship {
    Entity parent;            ///< Parent, or the null entity for a root.
    Entity firstChild;        ///< First child, or null.
    Entity lastChild;         ///< Last child, or null (children are appended in O(1)).
    Entity previousSibling;   ///< Previous child of the same parent, or null.
    Entity nextSibling;       ///< Next child of the same parent, or null.
    std::size_t children = 0; ///< Number of direct children.
};

} // namespace astra2d

/// @}
