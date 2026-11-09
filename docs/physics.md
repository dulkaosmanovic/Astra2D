# Physics {#physics_guide}

[TOC]

The @ref physics "physics" module is a 2D rigid body simulation written for Astra2D: shapes,
collision detection, an impulse solver, triggers, one-way platforms, sleeping, continuous collision
for fast bodies and tile map collision. It does not depend on OpenGL or the ECS, so all of it is
unit-tested. @ref astra2d::PhysicsSystem connects it to a @ref astra2d::Scene.

<div class="astra-summary"><b>In short</b>
<ul>
<li>Games add `Collider` and `RigidBody` components and a `PhysicsSystem`; tools can use `PhysicsWorld` directly.</li>
<li>A step runs broad phase → narrow phase (SAT, contact manifolds) → sequential impulse solver → sleeping.</li>
<li>Platformer features: one-way platforms, tile maps without ghost edges, bullets that do not tunnel, ray casts.</li>
<li>Deterministic: the same inputs give bit-identical results, whichever broad phase is used.</li>
</ul></div>

## Two ways to use it

**Through the scene** (what games normally do): give entities a @ref astra2d::Collider and, if they
move, a @ref astra2d::RigidBody, and add a PhysicsSystem after the scene.

```cpp
astra2d::Scene m_scene;
astra2d::PhysicsSystem m_physics{m_scene}; // declared after the scene: destroyed first

const auto ground = m_scene.createEntity("ground", {0.0F, -0.5F});
m_scene.registry().emplace<astra2d::Collider>(ground, astra2d::Collider{.halfExtents = {20.0F, 0.5F}});

const auto crate = m_scene.createEntity("crate", {0.0F, 5.0F});
m_scene.registry().emplace<astra2d::RigidBody>(crate);
m_scene.registry().emplace<astra2d::Collider>(crate);           // a 1x1 box
m_scene.registry().emplace<astra2d::Interpolated>(crate);       // smooth drawing

m_physics.onCollisionBegin().connect([](const astra2d::CollisionEvent& e) { /* e.a, e.b */ });
```

`scene.fixedUpdate(dt)` then runs the "physics" system, which steps the world and writes positions,
rotations and velocities back to the components.

**Directly**, without entities (tests, benchmarks, tools): @ref astra2d::PhysicsWorld with
@ref astra2d::BodyId and @ref astra2d::ShapeId handles.

```cpp
astra2d::PhysicsWorld world;
const auto ground = world.createBody({.type = astra2d::BodyType::Static});
world.addShape(ground, astra2d::ShapeDef::makeBox({20.0F, 0.5F}));
const auto ball = world.createBody({.position = {0.0F, 5.0F}});
world.addShape(ball, astra2d::ShapeDef::makeCircle(0.5F));
world.step(1.0F / 60.0F);
```

**Units.** The tolerances (allowed overlap, sleep speed, bounce threshold) are in metres, so the
defaults suit objects of 0.1 to 10 units. A game measured in pixels sets
`PhysicsSettings::lengthUnitsPerMeter` (e.g. 32 for 32-pixel tiles) and its gravity in pixels/s².

## One step

`world.step(dt)` runs at the fixed timestep (see @ref core_guide) and does this:

| Phase | What | Where |
|---|---|---|
| Broad phase | find pairs of shapes whose bounding boxes overlap | @ref astra2d::BroadPhase |
| Narrow phase | exact test of each pair: contact normal, points, depth (the manifold) | `collide*()` |
| Events | compare with the last step: contact/trigger begin and end | `contactBeginEvents()`, ... |
| Islands | group touching bodies; wake whole groups | union-find |
| Integrate velocities | gravity, forces, damping | |
| Solve velocities | sequential impulses: non-penetration, friction, restitution | |
| Solve positions | push overlapping bodies apart | split impulses |
| Integrate positions | move and rotate by the velocities | |
| Sleep | put groups at rest to sleep | |

## Shapes and the narrow phase

Bodies have one or more shapes: **circles**, **boxes** and **convex polygons** with up to 8
corners (@ref astra2d::makePolygon() builds the convex hull of any points). A box is a 4-corner
polygon that also has a fast path while its body is not rotated: then it is an axis-aligned
bounding box (AABB), the common case of platformer characters (`fixedRotation`).

Each pair gives a manifold (@ref astra2d::Manifold), with a unit **normal** from shape A to shape B, and up to two
**points** with a **separation** each (negative = penetration depth). Two convex shapes touch at a
point (a corner on an edge, two circles) or along a segment (edge on edge, e.g. a box lying flat),
and two points are enough to describe a segment.

| Pair | Method |
|---|---|
| circle–circle | distance of the centres |
| AABB–AABB | overlap on both axes; the normal is the axis of least penetration; the points are the ends of the overlap along the touching faces |
| AABB–circle | the closest point of the box to the centre (face or corner region); a centre inside is pushed out through the nearest face |
| polygon–polygon | **separating axis test** over the face normals of both; the face with the largest separation is the *reference face*, the most opposed face of the other polygon is the *incident face*, which is clipped to the reference face's sides to give two points |
| polygon–circle | the face of largest separation, then its corner regions |

The separating axis theorem says two convex shapes do not overlap if and only if there is an axis
on which their projections do not overlap; for polygons it is enough to try the face normals. The
largest separation found is the distance (if positive) or the penetration depth (if negative).
Every point carries a **feature id** (which faces and corners made it), so the solver can match it
with the same point in the next step.

All pair tests are in `include/astra2d/physics/collision.hpp` and are tested on their own
(`tests/physics/test_collision.cpp`), including the agreement of the AABB fast paths with the general
polygon code.

## Broad phase (E3)

Testing every pair of n shapes costs n(n−1)/2 narrow phase tests: 12.5 million for 5000 bodies,
although only a few hundred pairs touch. The broad phase finds the candidate pairs cheaply with
bounding boxes. Three methods are implemented, selected with `PhysicsSettings::broadPhase`:

| Method | Idea | Cost |
|---|---|---|
| `BruteForce` | test every pair of boxes | O(n²); the baseline |
| `SortAndSweep` | sort the boxes by their left edge; a box can only overlap boxes that start before it ends | O(n log n + k) |
| `UniformGrid` | put each box into the grid cells it covers (counting sort into hash buckets, no comparison sort); test only boxes sharing a cell, each pair once in the first cell they share | about O(n) for similar sizes |

Boxes larger than 256 cells (a long floor) are tested against all others instead of filling the
grid. Pairs of two bodies that cannot move (static, kinematic) are skipped by all methods. All
three report exactly the same pairs, sorted afterwards, so the simulation is **bit-identical
whatever broad phase is used** (tested); only the cost differs.

`benchmarks/physics_benchmark broadphase` measures this on a "gas" of N bodies bouncing in a box
without gravity (release build, Ryzen 5 3500U, mean step time):

| Bodies | Brute force | Sort and sweep | Uniform grid | Box tests: brute / SAP / grid |
|---:|---:|---:|---:|---|
| 100 | 0.049 ms | 0.026 ms | 0.032 ms | 5,350 / 611 / 154 |
| 1,000 | 2.72 ms | 0.20 ms | 0.16 ms | 503,500 / 15,523 / 1,403 |
| 5,000 | 62.4 ms | 1.57 ms | 0.92 ms | 12,517,500 / 160,676 / 6,851 |

With 100 bodies the broad phase does not matter; at 5000, brute force alone takes four frames
while the grid needs 1 ms for the whole step. Run `scripts/bench_physics.sh` for the full
experiment (several runs, CSV in `benchmarks/results/`).

## The solver

### Sequential impulses

Contacts are solved with **impulses**: instant changes of velocity, J/m for the linear and
(r × J)/I for the angular velocity, where r is the lever arm from the centre of mass to the contact
point. For each contact point, the normal impulse is the one that makes the relative velocity along
the normal zero:

    vn = (vB + ωB × rB − vA − ωA × rA) · n
    λ  = −vn / (1/mA + 1/mB + (rA × n)²/IA + (rB × n)²/IB)

Solving one point changes the velocities seen by the others, so all points are solved in turn,
**several times** (`velocityIterations`, default 8), converging to a solution for all of them. Two
details make this work well:

- **Accumulated clamping**: a contact can only push, so the impulse must be ≥ 0. The *sum* of the
  impulses of the current step is clamped, not each increment, so a later pass can take back part
  of what an earlier one applied.
- **Warm starting**: each point starts with its impulse from the last step (matched by feature id).
  A resting stack is then almost solved before the first pass.

**Friction** is an impulse along the tangent, limited by the Coulomb cone |λt| ≤ μ·λn, with
μ = √(μA·μB). Friction is solved before the normal impulse in each pass, since its limit depends on
it.

**Block solver.** A box lying flat has two contact points. Solving them one after the other makes
the order show: the first point pushes the box up a little and tilts it, and a tall stack slowly
leans. The two normal impulses are therefore solved together as a 2×2 linear complementarity
problem (both push, one pushes, none), as in Box2D. A tower of 20 boxes stays perfectly upright
(below), where the sequential version drifted 3 cm in 10 s with 10 boxes.

### Restitution

Bounciness e (the larger of the two shapes') is applied after the velocity iterations: points that
approached faster than `restitutionThreshold` get a separating velocity of e times their approach
speed. Below the threshold nothing bounces, so resting bodies do not jitter. The approach speed is
taken *before* gravity is added in the step, and a bouncing contact is not also pushed out by the
position correction: either would add energy on every bounce (a first version gained 18% in five
bounces).

### Removing overlap: split impulses

Discrete steps let bodies sink into each other a little. The overlap beyond `linearSlop` is removed
with a fraction (`positionCorrection`, 0.2) per step. Doing that with a velocity bias
(Baumgarte) adds the push-out speed to the real velocity, so bodies pop apart. Instead, the same
solver runs on separate **pseudo velocities** that move the bodies and are then dropped: no
kinetic energy is added. The slop keeps resting contacts slightly overlapping, so they persist
from step to step instead of flickering.

### Stability (E4)

`benchmarks/physics_benchmark stack` and `bounce` (10 simulated seconds, no sleeping):

| Tower | Sideways drift | Top box below its start | Largest tilt |
|---:|---:|---:|---:|
| 5 boxes | 0 | 0.006 | 0 |
| 10 boxes | 0 | 0.014 | 0 |
| 20 boxes | 0 | 0.064 | 0 |

The vertical error is the allowed overlap of every contact added up (and the bottom boxes carrying
the most weight overlap most). A ball dropped from 5 m with restitution 1 keeps its energy within
±2% over 10 s; with restitution 0 it stops at the first impact. @ref evaluation discusses both.

## Body types, sleeping, events

| Type | Moved by | Mass |
|---|---|---|
| `Static` | nothing (ground, walls) | infinite |
| `Dynamic` | gravity, forces, impulses, contacts | `mass` (> 0) |
| `Kinematic` | its velocity only (moving platforms) | infinite: pushes dynamic bodies, is not pushed |

A body's centre of mass and moment of inertia come from its shapes (uniform density, total mass
`BodyDef::mass`); `fixedRotation` makes the inertia infinite.

**Sleeping.** Bodies at rest cost nothing: after `timeToSleep` seconds below the sleep speeds a
body falls asleep and is no longer integrated or solved; contacts between sleeping (or static)
bodies are not recomputed. Bodies that touch form an **island** (union-find over the contacts),
and an island sleeps and wakes as a whole, so a stack does not freeze while the top box still
moves, and a ball dropped on it wakes the whole stack. Any setter, force or impulse wakes a body,
as do new contacts and moving kinematic bodies.

**Events.** After each step the world lists contacts that began and ended touching
(`contactBeginEvents()`, with normal, point and approach speed) and trigger overlaps that began and
ended. A trigger (`isTrigger`) reports overlaps but does not collide: pickups, goal zones, damage
areas. `PhysicsSystem` turns these into signals with entities. They are emitted after the step, so
handlers may create and destroy entities. `isGrounded()` asks whether a body stands on something
(a contact whose normal points up within a tolerance).

**Filtering.** Each shape has a `category` and a `mask` bit set; two shapes collide only if each
one's category is in the other's mask (e.g. player bullets ignore the player).
@ref astra2d::collisionGroups() combines groups into a mask, and @ref astra2d::withoutGroups()
removes some, for example while a player drops through platforms. They work at compile time and
without the casts that bit operations on `std::uint16_t` otherwise need:

```cpp
constexpr std::uint16_t kPlayerMask = astra2d::collisionGroups(kWorld, kPlatform, kEnemy);
collider.mask = dropping ? astra2d::withoutGroups(kPlayerMask, kPlatform) : kPlayerMask;
```

## Platformer features

**One-way platforms** (`oneWay`): a shape collides only with shapes arriving from its +y side
(rotated with its body) and not moving up. The decision is made when the two start touching, so a
character jumping up through a platform passes it entirely, then lands on it. A shape that was let
through starts colliding when it falls again with its bottom in the platform's upper half: a jump
that only just reaches the top still lands on it instead of dropping through. Only the edges that face
up give contact normals, so a character landing on the very end of a platform stands on it
instead of being pushed off sideways.

**Tile maps.** @ref astra2d::TileGrid holds solid and one-way tiles. @ref astra2d::mergeTiles()
covers them with few rectangles: each row is split into runs of equal tiles, and equal runs in
consecutive rows are merged. A flat floor is then one shape. The entity component is
@ref astra2d::TilemapCollider.

**Internal edges.** A box sliding along a wall made of several shapes can catch on the seams: where
it enters the next shape by a tiny amount from above, that shape's top face is the axis of least
penetration, and the box is stopped by a wall that does not exist ("ghost collision"). Every box
edge that is completely covered by solid tiles is therefore disabled (`ShapeDef::edgeMask`); the
narrow phase never picks a disabled edge as the normal (it still detects the overlap). This is
tested with a box sliding down a wall of separate tiles.

## Continuous collision (bullets)

At 60 steps per second, a body faster than its own size per step (here 60 m/s for a 1 m box) can be
on one side of a thin wall in one step and on the other side in the next, without ever overlapping
it: **tunnelling**. A body marked `bullet` uses **speculative contacts**: its broad phase box is
swept along its motion, and the narrow phase reports points up to the distance it moves in the step,
with a positive separation. The solver lets such a point close only that gap (vn ≥ −separation/dt),
so the bullet stops exactly at the surface. It is cheap (no time-of-impact search) and works
against every shape type. The cost: a fast body passing very close to a corner can be slowed by
it. So it is opt-in, for projectiles. A test shoots a ball at 300 m/s at a 10 cm wall: without
`bullet` it passes through, with it, it stops.

## Determinism

The world gives the same result for the same inputs: bodies, shapes and contacts are kept in
creation and key order, pairs are sorted before the narrow phase, and nothing depends on hash order
or addresses. Replays and lockstep networking need this; `tests/physics/test_physics.cpp` checks that two
runs, and runs with the three broad phases, give bit-identical positions. (Different compilers,
flags or CPUs may round differently.)

## The scene integration

@ref astra2d::PhysicsSystem keeps one body per entity with a Collider, TilemapCollider or
RigidBody (a Collider without RigidBody is static). Every fixed step it:

1. applies what the game changed since the last step: new or changed components, a moved
   Transform (a teleport), a new RigidBody velocity or type;
2. steps the world;
3. writes position, rotation and velocity back (converting to the parent's space for children);
4. emits `onCollisionBegin/End` and `onTriggerEnter/Exit`.

The system is a normal FixedUpdate system called "physics", added when the PhysicsSystem is
constructed: systems added before it (a player controller setting velocities) run before the step,
systems added after it see the results.

## Examples

<div class="astra-figures single">
<div class="astra-figure"><img src="physics_playground.png" alt="<code>examples/physics_playground</code> with debug drawing: a bullet hitting a pyramid, a tower of boxes"/><span><code>examples/physics_playground</code> with debug drawing: a bullet hitting a pyramid, a tower of boxes</span></div>
</div>

- `examples/physics_playground`: spawn boxes, circles and polygons with the mouse, drag them with
  a spring, shoot bullets, build towers and pyramids; one-way platforms, a tile map, a moving
  platform, a trigger zone; switch broad phases and see their cost; debug drawing of bounding
  boxes, contact points, normals and velocities.
- `examples/platformer`: a platformer with tile maps, one-way planks the player can drop through
  (collision groups), stomping enemies by the contact normal, hazards as triggers, ground detection
  and ledge detection with ray casts (see @ref platformer_guide).
- `examples/coin_collector`: a game without gravity; coins are triggers, spikes bounce off walls
  and the player by themselves, hits are collision events.

<div class="astra-pager">
<span class="astra-pager-prev">← @ref ecs_guide "ECS and scenes"</span>
<span class="astra-pager-next">Next: @ref audio_guide "Audio" →</span>
</div>
