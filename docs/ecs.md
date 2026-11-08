# ECS and scenes: entities, components, systems {#ecs_guide}

[TOC]

The @ref ecs "ecs" module stores game objects as **entities with components**; the
@ref scene "scene" module builds a game world on top of it: a Scene with systems, built-in
components, a parent/child hierarchy, rendering and scene files.

<div class="astra-summary"><b>In short</b>
<ul>
<li>An entity is a 32-bit id with a generation, a component is a plain struct, a system is a function over a view.</li>
<li>Each component type lives in a sparse set: O(1) add, remove and lookup, and gap-free iteration.</li>
<li>A `Scene` runs systems in an explicit order by phase and adds a hierarchy, interpolation and JSON scene files.</li>
<li>At 100,000 objects and more, updates are 2.6 to 6 times faster than with OOP game objects (E2).</li>
</ul></div>

## Why an entity component system

A classic game object hierarchy gives every object a class: `Player : Character : GameObject`, each
with a virtual `update()`. It reads naturally, but has two costs that grow with the game:

- **Inflexible.** Behaviour lives in the class tree. A "flying, collectible enemy" has to fit into
  one branch, so code is duplicated or pushed up into a base class that every object then carries.
- **Slow at scale.** Each object is a separate heap allocation reached through a pointer, with all
  of its data together. Updating positions touches whole objects scattered across memory, and every
  call goes through a virtual function.

An ECS splits the object up:

| Part | What it is | In Astra2D |
|---|---|---|
| Entity | an id, nothing else | @ref astra2d::Entity, 32 bits |
| Component | plain data, one struct per aspect | any movable struct, e.g. @ref astra2d::Transform |
| System | a function over all entities with certain components | a lambda over a @ref astra2d::View |

A flying, collectible enemy is an entity with `Transform`, `Flying`, `Pickup` and `Enemy`
components; no class has to know about the combination. Components of one type are stored
together in one array, so a system that moves things reads exactly the position and velocity
arrays, front to back.

```cpp
astra2d::Registry registry;
const astra2d::Entity ball = registry.create();
registry.emplace<Position>(ball, glm::vec2{0, 0});
registry.emplace<Velocity>(ball, glm::vec2{10, 0});

registry.view<Position, Velocity>().each([dt](Position& p, const Velocity& v) {
    p.value += v.value * dt;
});
```

## Entities and generations

An @ref astra2d::Entity packs a 20-bit **index** and a 12-bit **generation** into 32 bits. The
index selects a slot in the registry and in every component storage. When an entity is destroyed,
its slot goes on a free list and is reused by a later entity, with the generation increased by one.

A handle kept from before still has the old generation, so @ref astra2d::Registry::valid "valid()"
rejects it, instead of it silently pointing to the new entity in the same slot (the ABA problem of
plain indices). After 4095 reuses a slot is retired rather than wrapping its generation around to a
value an old handle might still hold. Up to 1 048 575 entities can be alive at once.

Misusing a handle (getting a component it does not have, using a destroyed entity) is a programmer
error and caught by `ASTRA_ASSERT`. @ref astra2d::Registry::tryGet "tryGet()" and `valid()` are the
checking variants.

## Sparse sets

Each component type is stored in its own @ref astra2d::ComponentPool, a **sparse set**:

```text
            entity index:   0    1    2    3    4    5
  sparse (index → slot):    –    0    –    2    1    –      pages of 4096, allocated on demand
                                 │         │    │
  dense entities:          [ e1 ][ e4 ][ e3 ]              packed, no gaps
  dense components:        [ c1 ][ c4 ][ c3 ]              same order
```

- `has` / `get`: one lookup in `sparse`, then one in `dense`. O(1).
- add: append to both dense arrays, write the slot. O(1).
- remove: move the **last** element into the gap and shrink ("swap and pop"). O(1), the arrays
  stay packed, but the order changes.
- iterate: walk the dense arrays from start to end, with no gaps, touching only memory in use.

The sparse array is split into pages that are only allocated when an entity in their range gets the
component, so a rare component on entities with high indices costs one page, not a full array.

References to components stay valid until a component of the **same type** is added (the array
may grow) or removed (the last one moves into the gap). Keep entities, not pointers, between frames.

## Views

`registry.view<A, B>()` visits the entities that have all of A and B. It walks the **smallest** of
the pools and checks the others, so `view<Transform, Boss>()` takes as many steps as there are
bosses, not transforms. For each visited entity:

- in the leading pool, the component is at the loop index, with no lookup;
- in every other pool, it first checks the **same position**: entities that got their components
  in the same order sit at the same place in every pool, so this is usually a sequential read; only
  otherwise does it do a sparse lookup.

That shortcut matters: in the E2 benchmark it took the update of a million entities from 3.9 ms to
2.6 ms.

### Changing the registry while iterating

Views visit entities **from the end of the pool to the front**. Removing the visited entity moves
the last element (already visited) into its slot, so nothing is skipped or visited twice. While
iterating, it is therefore safe to:

- remove components from, or destroy, the entity being visited;
- add components of types the view does not iterate, to any entity;
- create entities (they are not visited).

Destroying *other* entities, or adding components of the iterated types, can move an entity that
was already visited into a position still ahead, so it would be visited twice. For that,
@ref astra2d::Scene::destroyLater "Scene::destroyLater()" queues the entity and destroys it after
the systems have run. The tests in `tests/test_ecs.cpp` check each of these cases.

## Signals

Every pool has two signals, @ref astra2d::Registry::onConstruct "onConstruct<T>()" (after a
component is added) and @ref astra2d::Registry::onDestroy "onDestroy<T>()" (before it is removed,
also when the entity is destroyed, while it can still be read):

```cpp
astra2d::Connection connection = registry.onDestroy<Pickup>().connect(
    [this](astra2d::Registry& registry, astra2d::Entity coin) {
        m_score += registry.get<Pickup>(coin).value;   // whoever removed the coin
    });
```

A @ref astra2d::Connection disconnects its handler when destroyed, like an event subscription.

Handlers run in the middle of a change to the registry, so a few rules keep that change consistent.
The tests in `tests/test_ecs.cpp` cover each of them:

- Handlers may create and destroy entities, and add or remove components of other entities. That
  includes component types used for the first time.
- An `onDestroy` handler may read the dying entity's components, but must not add new ones to it
  (an assertion). In release builds, any that are added anyway are dropped, so nothing outlives the
  entity. Destroying the dying entity again is harmless.
- An `onConstruct` handler may remove the component it was told about (to reject it). `emplace()`
  then throws `std::logic_error`, because it has no component left to return.
- Emitting does not copy the handler list. Handlers connected during an emit first run on the next
  one, and disconnected ones are skipped at once.

Signals let independent code react to changes without the code making them knowing about it; the
Scene uses one itself to keep the hierarchy consistent (below).

## Scene and systems

@ref astra2d::Scene owns a Registry and runs **systems** in a fixed, explicit order: by phase, then
in the order they were added.

| Phase | Run by | Typical systems |
|---|---|---|
| `FixedUpdate` | `scene.fixedUpdate(dt)` from Application::onFixedUpdate() | movement, gameplay, physics (@ref astra2d::PhysicsSystem) |
| `Update` | `scene.update(dt)` from Application::onUpdate() | input, AI |
| `LateUpdate` | `scene.update(dt)`, after Update | animation (built in), camera follow |

```cpp
m_scene.addSystem(astra2d::SystemPhase::FixedUpdate, "spin", [](astra2d::Scene& scene, float dt) {
    scene.registry().view<astra2d::Transform, Spin>().each(
        [dt](astra2d::Transform& t, const Spin& s) { t.rotation += s.speed * dt; });
});
```

An explicit order instead of priorities or dependency graphs keeps a frame easy to follow: the
order is the list `systemNames()` returns. Entities queued with `destroyLater()` are destroyed at
the end of `fixedUpdate()` and `update()`.

### Built-in components

| Component | Purpose |
|---|---|
| @ref astra2d::Name | a name for `Scene::find()`, debugging and scene files |
| @ref astra2d::Transform | position, rotation, scale, relative to the parent; `createEntity()` adds it |
| @ref astra2d::SpriteRenderer | sprite (or solid quad), size, tint, layer, flip, visibility |
| @ref astra2d::Animator | plays a sprite sheet animation into the SpriteRenderer |
| @ref astra2d::RigidBody | body type, velocity, mass, gravity scale; simulated by @ref astra2d::PhysicsSystem |
| @ref astra2d::Collider | box, circle or polygon shape, material, trigger / one-way flags, collision groups |
| @ref astra2d::TilemapCollider | collision for a grid of solid and one-way tiles |
| @ref astra2d::Relationship | hierarchy links, managed by `Scene::setParent()` |
| @ref astra2d::Interpolated | opt-in smooth drawing of entities moved at the fixed timestep |

`scene.render(renderer)` draws every visible SpriteRenderer at its entity's world transform, lower
layers first; within a layer parents are drawn before children. It is called between
`Renderer2D::begin()` and `end()`, so the game still chooses the camera.

### Smooth motion with interpolation

Systems in `FixedUpdate` move entities 60 times per second, but a 144 Hz display shows more frames
than that, so an entity drawn at its latest Transform moves in visible jumps. Add
@ref astra2d::Interpolated to such an entity and pass the application's interpolation alpha to
`render()`:

```cpp
registry.emplace<astra2d::Interpolated>(player);
// ...
m_scene.render(m_renderer, interpolationAlpha());
const glm::vec2 cameraTarget = m_scene.worldTransform(player, interpolationAlpha()).position;
```

`fixedUpdate()` remembers each such entity's Transform before the step runs. `render()` and
`worldTransform(entity, alpha)` blend between that and the current Transform, so the entity is
drawn where it was a fraction `alpha` of the way through the step. After an instant move (respawn,
teleport), call `scene.resetInterpolation(entity)` so it does not slide there. It is opt-in, because
entities that never move at the fixed rate gain nothing from it.

## Hierarchy

`scene.setParent(child, parent)` makes a child's Transform relative to its parent: moving, turning
or scaling the parent carries the whole branch. A solar system is three levels deep: the sun, an
invisible spinning "orbit" entity, and the planet at a fixed distance on it.

```text
  world = compose(parentWorld, local)
        position = parent.position + rotate(local.position * parent.scale, parent.rotation)
        rotation = parent.rotation + local.rotation
        scale    = parent.scale * local.scale
```

- Children are a doubly linked list inside the @ref astra2d::Relationship component (first child,
  previous and next sibling), so attaching and detaching need no extra allocations.
- `setParent(child, parent, /*keepWorldTransform=*/true)` changes the child's Transform with
  @ref astra2d::relativeTo so it stays where it is on screen.
- World transforms are computed top-down in `render()`, once per frame, and on demand with
  `worldTransform(entity)` for gameplay code.
- `destroyEntity()` destroys the entity and all its descendants. Even `registry.destroy()` on a
  parent leaves no dangling links: an `onDestroy<Relationship>` handler unlinks it and turns its
  children into roots.
- Walking the hierarchy uses loops with an explicit stack, not recursion, so a very deep hierarchy
  cannot overflow the call stack.

## Scene files

@ref astra2d::SceneSerializer saves a scene to JSON (with nlohmann/json) and loads it back:

```json
{
  "format": "astra2d-scene", "version": 1,
  "entities": [
    { "id": 0, "parent": null, "components": { "Name": "sun", "Transform": { "position": [0, 0] } } },
    { "id": 1, "parent": 0,    "components": { "Name": "earth orbit", "Spin": { "speed": 0.6 } } }
  ]
}
```

- Entities are written depth-first, so loading rebuilds the same hierarchy with siblings in order.
- Entity handles change on loading, so references are stored as file ids:
  `SaveContext::id(entity)` and `LoadContext::entity(json)` convert them.
- Textures are stored by path: an @ref astra2d::AssetCache remembers which file and settings each
  texture came from (@ref astra2d::AssetCache::textureSource "textureSource()").
- Missing fields keep the component's default, and unknown components are skipped with a warning,
  so old files keep loading when components gain fields.
- Loading is all or nothing: on any error the entities created so far are destroyed again and the
  exception names the entity and the component.
- Floats are written in their shortest exact form (`0.1`, not `0.10000000149011612`) and read back
  bit for bit.

Game components are added with `registerComponent<T>(name, save, load)`. Components that point to
run-time data a file cannot hold, such as an Animator's animation, are registered by the game with
a name it can map back; the `scene` example does exactly this.

## Performance: ECS vs. game objects (E2)

`scripts/bench_ecs.sh` runs `benchmarks/ecs_benchmark`: N objects with a position, a velocity and
some data the update does not touch, updated as `position += velocity * dt`, stored three ways:

- `ecs`: four components, updated by a `View<Position, Velocity>`;
- `oop`: one heap-allocated `GameObject` per object with a virtual `update()`, allocated in order;
- `oop_shuffled`: the same objects updated in random order, as in a long-running game whose objects
  were created and destroyed over time.

Time per object and update (release build, AMD Ryzen 5 3500U, mean of 5 runs):

| Objects | ecs | oop | oop_shuffled |
|---:|---:|---:|---:|
| 10,000 | 3.6 ns | 2.2 ns | 4.2 ns |
| 100,000 | 2.07 ns | 7.07 ns | 12.5 ns |
| 1,000,000 | 2.60 ns | 6.84 ns | 12.9 ns |

While everything fits in the CPU cache (10,000 objects) the layouts cost about the same, within the
measurement noise. Beyond that, the ECS stays flat, while the game objects get 2.6 to 3.4 times
slower in allocation order and 5 to 6 times slower shuffled, because each object is a cache miss
and carries data the update does not need. @ref evaluation has the chart and the discussion.

## Examples

`./build/examples/coin_collector` is a small complete game on the Scene: the player, coins,
spikes and effects are entities with engine components (Transform, SpriteRenderer, Animator,
RigidBody, Collider, Interpolated) and the game's own (Player, Coin, Spike, Burst), the gameplay is
systems, and coin pickups and spike hits are physics events (see @ref physics_guide). The Sandbox
keeps its world in a Scene too.

`./build/examples/scene` shows all of it: the solar system hierarchy, animated coins with a
`Pickup` component, an `onDestroy<Pickup>` signal that counts collected coins and spawns an effect,
`destroyLater()` in a view, and saving (F5) and loading (F9) the scene, including the example's own
components.

<div class="astra-pager">
<span class="astra-pager-prev">← @ref renderer_guide "Renderer"</span>
<span class="astra-pager-next">Next: @ref physics_guide "Physics" →</span>
</div>
