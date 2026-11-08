#pragma once

#include "astra2d/physics/broad_phase.hpp"
#include "astra2d/physics/collision.hpp"
#include "astra2d/physics/geometry.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

/// @addtogroup physics
/// @{

namespace astra2d {

/// How the physics treats a body.
enum class BodyType {
    Static,    ///< Never moves (ground, walls); infinite mass.
    Dynamic,   ///< Moved by gravity, forces and collisions.
    Kinematic, ///< Moved only by its velocity, set by the game (moving platforms); pushes others.
};

/// Refers to a body of a PhysicsWorld. Stays invalid after the body is destroyed, also when its
/// slot is reused (like Entity, it has a generation).
struct BodyId {
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max(); ///< Slot in the world.
    std::uint32_t generation = 0;                                    ///< Use count of the slot.

    /// True only if default-constructed (a non-null id may still refer to a destroyed body).
    [[nodiscard]] bool isNull() const { return index == std::numeric_limits<std::uint32_t>::max(); }
    /// Equal if all fields are equal.
    friend bool operator==(BodyId, BodyId) = default;
};

/// Refers to a shape of a PhysicsWorld; like BodyId.
struct ShapeId {
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max(); ///< Slot in the world.
    std::uint32_t generation = 0;                                    ///< Use count of the slot.

    /// True only if default-constructed (a non-null id may still refer to a destroyed shape).
    [[nodiscard]] bool isNull() const { return index == std::numeric_limits<std::uint32_t>::max(); }
    /// Equal if all fields are equal.
    friend bool operator==(ShapeId, ShapeId) = default;
};

/// How to create a body.
struct BodyDef {
    BodyType type = BodyType::Dynamic; ///< Static, dynamic or kinematic.
    glm::vec2 position{0.0F};          ///< Origin in world units.
    float angle = 0.0F;                ///< Counter-clockwise rotation in radians.
    glm::vec2 velocity{0.0F};          ///< Linear velocity of the centre of mass, units/s.
    float angularVelocity = 0.0F;      ///< Radians per second, counter-clockwise.
    float mass = 1.0F;                 ///< Mass of a dynamic body (> 0); ignored otherwise.
    float gravityScale = 1.0F;         ///< Multiplies the world's gravity (0 = floats).
    float linearDamping = 0.0F;        ///< Fraction of velocity lost per second (air drag).
    float angularDamping = 0.0F;       ///< Fraction of angular velocity lost per second.
    bool fixedRotation = false;        ///< Never rotates (e.g. a platformer character).
    /// Continuous collision: never tunnels through other shapes, however fast (see the physics
    /// guide). Costs a little more and can catch on corners it passes closely, so use it for fast
    /// projectiles only.
    bool bullet = false;
    bool allowSleep = true; ///< May fall asleep when at rest.
    bool awake = true;      ///< Start awake.
};

/// The geometry of a shape.
enum class ShapeType {
    Circle,  ///< ShapeDef::circle.
    Box,     ///< ShapeDef::polygon made by makeBox(); axis-aligned while its body is not rotated.
    Polygon, ///< ShapeDef::polygon, any convex polygon.
};

/// How to create a shape. Use the makeBox(), makeCircle() and makePolygon() helpers, then set the
/// material.
struct ShapeDef {
    ShapeType type = ShapeType::Box; ///< Which geometry is used.
    Circle circle;                   ///< For ShapeType::Circle, in body coordinates.
    Polygon polygon =
        astra2d::makeBox(glm::vec2(0.5F)); ///< For Box and Polygon, in body coordinates.
    float friction = 0.5F;    ///< Coulomb friction; a pair uses the geometric mean of both.
    float restitution = 0.0F; ///< Bounciness: 0 = no bounce, 1 = elastic; a pair uses the larger.
    /// Reports overlaps (trigger events) but does not collide, e.g. a pickup or a goal zone.
    bool isTrigger = false;
    /// A one-way platform: collides only with shapes coming from its body's +y side (rotated with
    /// the body), so a character can jump up through it and land on top.
    bool oneWay = false;
    std::uint16_t category = 0x0001; ///< Which collision groups this shape is in (bits).
    std::uint16_t mask = 0xFFFF;     ///< Which groups it collides with; both shapes must agree.
    /// Edges that can give a contact normal (bit i = edge i; see kBoxBottom). Clear the bits of
    /// edges shared with a neighbour, e.g. between tiles (done by addTileShapes()).
    std::uint8_t edgeMask = 0xFF;

    /// A box of half size @p halfExtents centred at @p center.
    [[nodiscard]] static ShapeDef makeBox(glm::vec2 halfExtents,
                                          glm::vec2 center = glm::vec2(0.0F));
    /// A circle.
    [[nodiscard]] static ShapeDef makeCircle(float radius, glm::vec2 center = glm::vec2(0.0F));
    /// The convex hull of @p points (see makePolygon()).
    [[nodiscard]] static ShapeDef makePolygon(std::span<const glm::vec2> points);
};

/// Settings of a PhysicsWorld. The defaults suit sizes of about 0.1 to 10 units, as with metres;
/// for other units set lengthUnitsPerMeter, which scales the length tolerances.
struct PhysicsSettings {
    glm::vec2 gravity{0.0F, -9.81F}; ///< Acceleration of dynamic bodies, units/s².
    /// How many world units are one metre, e.g. 32 for a game measured in pixels with 32 pixel
    /// tiles. Multiplies linearSlop, speculativeDistance, restitutionThreshold and
    /// linearSleepTolerance (but not gravity).
    float lengthUnitsPerMeter = 1.0F;
    int velocityIterations = 8; ///< Solver passes for velocities; more = stiffer stacks.
    int positionIterations = 3; ///< Solver passes for removing overlap.
    /// Overlap allowed without correction, in metres; keeps resting contacts from flickering.
    float linearSlop = 0.005F;
    /// Fraction of the remaining overlap removed per step (0 to 1).
    float positionCorrection = 0.2F;
    /// Shapes closer than this (metres) already get contact points, which stops them before they
    /// overlap. Bullets use their motion in the step instead. 0 keeps resting contacts simple.
    float speculativeDistance = 0.0F;
    /// Impacts slower than this (metres/s) do not bounce, so resting bodies do not jitter.
    float restitutionThreshold = 0.5F;
    bool enableSleep = true;              ///< Let bodies at rest fall asleep.
    float timeToSleep = 0.5F;             ///< Seconds at rest before a body falls asleep.
    float linearSleepTolerance = 0.05F;   ///< Metres/s below which a body counts as at rest.
    float angularSleepTolerance = 0.035F; ///< Radians/s below which a body counts as at rest.
    BroadPhaseType broadPhase = BroadPhaseType::UniformGrid; ///< How pairs are found.
    /// Cell size of BroadPhaseType::UniformGrid in world units; 0 picks one from the bodies.
    float gridCellSize = 0.0F;
};

/// A contact between two solid shapes started touching (in the last step).
struct ContactBeginEvent {
    ShapeId shapeA;             ///< First shape.
    ShapeId shapeB;             ///< Second shape.
    BodyId bodyA;               ///< Body of shapeA.
    BodyId bodyB;               ///< Body of shapeB.
    glm::vec2 normal{0.0F};     ///< Unit vector from A to B.
    glm::vec2 point{0.0F};      ///< A contact point.
    float approachSpeed = 0.0F; ///< How fast they moved towards each other along the normal.
};

/// Two solid shapes stopped touching, or one of them was destroyed (its ids are then stale).
struct ContactEndEvent {
    ShapeId shapeA; ///< First shape.
    ShapeId shapeB; ///< Second shape.
    BodyId bodyA;   ///< Body of shapeA.
    BodyId bodyB;   ///< Body of shapeB.
};

/// A shape started or stopped overlapping a trigger (on exit, either may already be destroyed).
struct TriggerEvent {
    ShapeId trigger;    ///< The trigger shape.
    ShapeId other;      ///< The shape that entered or left it.
    BodyId triggerBody; ///< Body of the trigger.
    BodyId otherBody;   ///< Body of the other shape.
};

/// A contact between two shapes, for debug drawing and queries. Valid until the next step.
struct ContactInfo {
    ShapeId shapeA;        ///< First shape.
    ShapeId shapeB;        ///< Second shape.
    BodyId bodyA;          ///< Body of shapeA.
    BodyId bodyB;          ///< Body of shapeB.
    Manifold manifold;     ///< Normal (A to B) and points; impulses from the last step.
    bool touching = false; ///< Overlapping and solid (not a trigger, not a passed one-way contact).
};

/// The closest hit of PhysicsWorld::rayCast().
struct WorldRayHit {
    ShapeId shape;  ///< The shape hit.
    BodyId body;    ///< Its body.
    RayCastHit hit; ///< Point, normal, fraction.
};

/// What the last step did, for profiling and the evaluation.
struct PhysicsStats {
    int bodies = 0;             ///< Bodies in the world.
    int awakeBodies = 0;        ///< Dynamic bodies that were simulated.
    int shapes = 0;             ///< Shapes in the world.
    std::int64_t pairTests = 0; ///< Bounding box overlap tests done by the broad phase.
    int candidatePairs = 0;     ///< Pairs passed to the narrow phase.
    int touchingContacts = 0;   ///< Contacts with solid touching shapes.
    double broadPhaseMs = 0.0;  ///< Time to find pairs.
    double narrowPhaseMs = 0.0; ///< Time to compute manifolds and events.
    double solverMs = 0.0;      ///< Time to integrate and solve contacts.
    double stepMs = 0.0;        ///< Time of the whole step.
};

/// A 2D rigid body simulation: bodies with shapes, gravity, collisions with friction and
/// restitution, triggers, one-way platforms, sleeping and continuous collision for bullets.
///
/// @code
/// astra2d::PhysicsWorld world;
/// const auto ground = world.createBody({.type = astra2d::BodyType::Static});
/// world.addShape(ground, astra2d::ShapeDef::makeBox({10.0F, 0.5F}));
/// const auto ball = world.createBody({.position = {0.0F, 5.0F}});
/// world.addShape(ball, astra2d::ShapeDef::makeCircle(0.5F));
/// world.step(1.0F / 60.0F); // at the fixed timestep
/// @endcode
///
/// A step finds pairs of shapes whose bounding boxes overlap (broad phase), computes their contact
/// manifolds (narrow phase), applies gravity and forces, solves the contacts with sequential
/// impulses (warm-started from the last step) and moves the bodies. See @ref physics_guide.
///
/// Stepping is deterministic: the same world stepped with the same time steps gives the same
/// results, whatever broad phase is used. Not thread-safe. The world does not use OpenGL.
class PhysicsWorld {
public:
    /// An empty world with @p settings.
    explicit PhysicsWorld(const PhysicsSettings& settings = {});
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    /// Takes over the bodies of another world; its handles stay valid for this one.
    PhysicsWorld(PhysicsWorld&&) noexcept;
    /// Replaces this world with another one.
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    /// The settings; changes take effect at the next step (the broad phase type too).
    [[nodiscard]] PhysicsSettings& settings();
    /// The settings.
    [[nodiscard]] const PhysicsSettings& settings() const;

    // --- Bodies ---------------------------------------------------------------------------------

    /// Creates a body without shapes.
    BodyId createBody(const BodyDef& def = {});
    /// Destroys @p body and its shapes; touching contacts end (ContactEndEvent / TriggerEvent in
    /// the next step) and bodies resting on it wake up. @pre valid(body).
    void destroyBody(BodyId body);
    /// True if @p body exists.
    [[nodiscard]] bool valid(BodyId body) const;
    /// Number of bodies.
    [[nodiscard]] std::size_t bodyCount() const;
    /// Calls @p function for every body, in creation order (oldest slot first).
    void forEachBody(const std::function<void(BodyId)>& function) const;

    // Body state. All functions require a valid body. Setters wake the body if it changes.

    [[nodiscard]] BodyType type(BodyId body) const; ///< Static, dynamic or kinematic.
    void setType(BodyId body,
                 BodyType type); ///< Changes the type; resets the velocity of static bodies.
    [[nodiscard]] glm::vec2 position(BodyId body) const; ///< Origin in world units.
    [[nodiscard]] float angle(BodyId body) const;        ///< Rotation in radians.
    [[nodiscard]] Pose pose(BodyId body) const;          ///< Origin and rotation.
    /// Moves the body instantly (teleport); other bodies are pushed out at the next step.
    void setTransform(BodyId body, glm::vec2 position, float angle);
    [[nodiscard]] glm::vec2 worldCenter(BodyId body) const; ///< Centre of mass in world units.
    [[nodiscard]] glm::vec2 velocity(BodyId body) const;    ///< Velocity of the centre of mass.
    void setVelocity(BodyId body,
                     glm::vec2 velocity); ///< Sets the velocity (not of static bodies).
    [[nodiscard]] float angularVelocity(BodyId body) const;      ///< Radians per second.
    void setAngularVelocity(BodyId body, float angularVelocity); ///< Sets the spin.
    [[nodiscard]] float mass(BodyId body) const;    ///< Mass; 0 for static and kinematic bodies.
    void setMass(BodyId body, float mass);          ///< Mass of a dynamic body. @pre mass > 0.
    [[nodiscard]] float inertia(BodyId body) const; ///< Moment of inertia about the centre of mass.
    [[nodiscard]] float gravityScale(BodyId body) const;       ///< Multiplies gravity.
    void setGravityScale(BodyId body, float scale);            ///< Sets the gravity factor.
    void setDamping(BodyId body, float linear, float angular); ///< Sets the drag.
    [[nodiscard]] bool fixedRotation(BodyId body) const;       ///< True if it never rotates.
    void setFixedRotation(BodyId body, bool fixed);            ///< Locks or unlocks rotation.
    void setBullet(BodyId body, bool bullet);      ///< Turns continuous collision on or off.
    [[nodiscard]] bool isAwake(BodyId body) const; ///< False while asleep.
    /// Wakes the body up or puts it to sleep now (with zero velocity).
    void setAwake(BodyId body, bool awake);
    void setAllowSleep(BodyId body, bool allow); ///< Whether it may fall asleep.

    /// Adds @p force (units·mass/s²) at the world point @p point for the next step.
    void applyForce(BodyId body, glm::vec2 force, glm::vec2 point);
    /// Adds @p force at the centre of mass for the next step (no torque).
    void applyForceToCenter(BodyId body, glm::vec2 force);
    /// Adds a torque for the next step.
    void applyTorque(BodyId body, float torque);
    /// Changes the velocity at once by @p impulse / mass, as if hit at the world point @p point.
    void applyImpulse(BodyId body, glm::vec2 impulse, glm::vec2 point);
    /// Changes the velocity at once by @p impulse / mass.
    void applyImpulseToCenter(BodyId body, glm::vec2 impulse);
    /// Changes the angular velocity at once by @p impulse / inertia.
    void applyAngularImpulse(BodyId body, float impulse);

    // --- Shapes ---------------------------------------------------------------------------------

    /// Adds a shape to @p body and updates the body's centre of mass and inertia (the mass stays
    /// BodyDef::mass, spread evenly over the area of all shapes). @pre valid(body); for Box and
    /// Polygon, def.polygon has at least 3 corners; for Circle, a positive radius.
    ShapeId addShape(BodyId body, const ShapeDef& def);
    /// Removes @p shape; its touching contacts end. @pre valid(shape).
    void removeShape(ShapeId shape);
    /// Removes all shapes of @p body (much faster than removeShape() one by one for many
    /// shapes); their touching contacts end. @pre valid(body).
    void removeShapes(BodyId body);
    /// True if @p shape exists.
    [[nodiscard]] bool valid(ShapeId shape) const;
    /// The body of @p shape. @pre valid(shape).
    [[nodiscard]] BodyId body(ShapeId shape) const;
    /// The shapes of @p body, in the order they were added. @pre valid(body).
    [[nodiscard]] std::vector<ShapeId> shapes(BodyId body) const;
    /// The definition of @p shape (geometry in body coordinates and material). @pre valid(shape).
    [[nodiscard]] const ShapeDef& shapeDef(ShapeId shape) const;
    /// Changes friction and restitution of @p shape. @pre valid(shape).
    void setMaterial(ShapeId shape, float friction, float restitution);
    /// Bounding box of @p shape at its body's current pose. @pre valid(shape).
    [[nodiscard]] Aabb shapeAabb(ShapeId shape) const;

    // --- Simulation -----------------------------------------------------------------------------

    /// Advances the world by @p deltaTime seconds; call it at a fixed rate. Does nothing for 0.
    void step(float deltaTime);

    /// What the last step did.
    [[nodiscard]] const PhysicsStats& stats() const;

    /// Contacts that started touching in the last step. Valid until the next step.
    [[nodiscard]] std::span<const ContactBeginEvent> contactBeginEvents() const;
    /// Contacts that stopped touching in the last step (or whose shape was destroyed before it).
    [[nodiscard]] std::span<const ContactEndEvent> contactEndEvents() const;
    /// Shapes that started overlapping a trigger in the last step.
    [[nodiscard]] std::span<const TriggerEvent> triggerBeginEvents() const;
    /// Shapes that stopped overlapping a trigger in the last step (or were destroyed before it).
    [[nodiscard]] std::span<const TriggerEvent> triggerEndEvents() const;

    /// Every pair of shapes whose bounding boxes overlapped in the last step, with its manifold.
    [[nodiscard]] std::vector<ContactInfo> contacts() const;
    /// The touching contacts of @p body; each normal points away from @p body.
    /// @pre valid(body).
    [[nodiscard]] std::vector<ContactInfo> contacts(BodyId body) const;

    /// True if @p body stands on something: a touching solid contact whose normal, pointing towards
    /// @p body, is within acos(@p minDot) of @p up. Uses the contacts of the last step.
    /// @pre valid(body).
    [[nodiscard]] bool isGrounded(BodyId body, glm::vec2 up = {0.0F, 1.0F},
                                  float minDot = 0.7F) const;

    // --- Queries --------------------------------------------------------------------------------

    /// The closest shape hit by the ray from @p origin to `origin + translation`. Triggers are
    /// skipped; only shapes whose category is in @p mask count. O(shapes).
    [[nodiscard]] std::optional<WorldRayHit> rayCast(glm::vec2 origin, glm::vec2 translation,
                                                     std::uint16_t mask = 0xFFFF) const;
    /// Shapes whose bounding box overlaps @p box, in index order. O(shapes).
    [[nodiscard]] std::vector<ShapeId> queryAabb(const Aabb& box) const;
    /// Shapes that contain @p point (triggers too), in index order. O(shapes).
    [[nodiscard]] std::vector<ShapeId> queryPoint(glm::vec2 point) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace astra2d

/// @}
