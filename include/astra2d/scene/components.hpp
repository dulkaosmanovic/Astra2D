#pragma once

#include "astra2d/ecs/entity.hpp"
#include "astra2d/renderer/animation.hpp"
#include "astra2d/renderer/sprite.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <string>

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
};

/// How the physics treats a RigidBody.
enum class BodyType {
    Static,    ///< Never moves (ground, walls); infinite mass.
    Dynamic,   ///< Moved by gravity, forces and collisions.
    Kinematic, ///< Moved only by its velocity, set by the game (moving platforms); pushes others.
};

/// Physical motion of an entity. Data only for now; the physics module (next) moves bodies and
/// writes the result to Transform.
struct RigidBody {
    BodyType type = BodyType::Dynamic; ///< Static, dynamic or kinematic.
    glm::vec2 velocity{0.0F};          ///< World units per second.
    float mass = 1.0F;                 ///< Mass of a dynamic body (> 0).
    float gravityScale = 1.0F;         ///< Multiplies the world's gravity (0 = floats).
    float linearDamping = 0.0F;        ///< Fraction of velocity lost per second (air drag).
};

/// The shape of a Collider.
enum class ColliderShape {
    Box,    ///< Axis-aligned box with Collider::halfExtents.
    Circle, ///< Circle with Collider::radius.
};

/// The collision shape of an entity, centred on its position plus @p offset. Data only for now;
/// used by the physics module (next).
struct Collider {
    ColliderShape shape = ColliderShape::Box; ///< Box or circle.
    glm::vec2 halfExtents{0.5F};              ///< Half width and height of a box.
    float radius = 0.5F;                      ///< Radius of a circle.
    glm::vec2 offset{0.0F};                   ///< Centre relative to the entity's position.
    float restitution = 0.0F;                 ///< Bounciness: 0 = no bounce, 1 = elastic.
    float friction = 0.5F;                    ///< Friction coefficient.
    bool isTrigger = false;                   ///< Reports overlaps but does not collide.
};

/// Links an entity into the scene hierarchy. Managed by Scene::setParent(); do not change it
/// directly. Children form a doubly linked list, so attaching and detaching is O(1) apart from
/// finding the last child.
struct Relationship {
    Entity parent;            ///< Parent, or the null entity for a root.
    Entity firstChild;        ///< First child, or null.
    Entity previousSibling;   ///< Previous child of the same parent, or null.
    Entity nextSibling;       ///< Next child of the same parent, or null.
    std::size_t children = 0; ///< Number of direct children.
};

} // namespace astra2d

/// @}
