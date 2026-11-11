# Example game: Astra Run {#platformer_guide}

[TOC]

`examples/platformer` is the showcase of Astra2D: a complete platformer written only against the
public API in `include/astra2d/`. An astronaut crosses four planets back to Earth, one level each.
The levels come from a text map and from Tiled maps. The player meets enemies, moving platforms,
hazards and checkpoints. Rendering, input, the ECS, physics, level loading and audio all come from
the engine.

This page is about **how the engine is used to build it**: which module does what, and the calls
the game makes. The game's own rules (movement tuning, enemies, levels) are in the commented source.

<div class="astra-figures">
<div class="astra-figure"><img src="platformer_level1.png" alt="Planet Verda: a jump to the coins"/><span>Planet Verda: a Scene with sprites, parallax layers and a tile map</span></div>
<div class="astra-figure"><img src="platformer_debug.png" alt="The same jump with the physics debug view"/><span>The same moment with the physics debug view (F3)</span></div>
<div class="astra-figure"><img src="platformer_level2.png" alt="The Crystal Moon, a Tiled map"/><span>The Crystal Moon, loaded from a Tiled map</span></div>
<div class="astra-figure"><img src="platformer_level3.png" alt="Nimbus: one-way clouds"/><span>Nimbus: one-way tiles and a moving one-way body</span></div>
<div class="astra-figure"><img src="platformer_level4.png" alt="Pyros: hazards as triggers"/><span>Pyros: lava and spikes are physics triggers</span></div>
<div class="astra-figure"><img src="platformer_ending.png" alt="The ending on Earth"/><span>The ending: sprites drawn in screen space, rotated along a curve</span></div>
</div>

```bash
./build/examples/platformer        # A/D run, Space jump, S+Space drop, P pause, F3 physics view
./build/examples/platformer 2      # start at level 2, e.g. while editing it
```

<div class="astra-summary"><b>In short</b>
<ul>
<li>2,173 lines of game code on top of the engine, 17 % of the engine's size (@ref evaluation "E5"); it uses 54 engine names from all eight modules.</li>
<li>The game is an <code>Application</code>, its objects are entities in a <code>Scene</code>, its logic is Scene systems, and it reacts to physics through signals.</li>
<li>The parts without OpenGL (level loading, movement rules) are unit-tested.</li>
</ul></div>

## Code structure

| File | Lines | What |
|---|---|---|
| `main.cpp`, `game.hpp` | 79 | Entry point: reads the level number, runs the game |
| `game.cpp` | 1,594 | The `Application`: entities, systems, physics events, camera, drawing, HUD, sound |
| `level.hpp/.cpp` | 568 | Levels as data: text maps and Tiled maps into tile grids and spawn points |
| `controller.hpp/.cpp` | 136 | Movement rules: acceleration, jump, coyote time, jump buffering |
| `looks.hpp` | 151 | Each planet's sky and parallax layers, as plain data |

`level` and `controller` do not use OpenGL, so `tests/examples/test_platformer.cpp` tests them
directly.

## The application and input (core)

The game derives from @ref astra2d::Application and fills in its hooks. The engine runs the loop
with a fixed timestep: physics and gameplay advance in `onFixedUpdate()` at 60 steps per second,
and `onRender()` draws as often as the screen allows, with sprites interpolated between steps.

```cpp
class Game : public astra2d::Application {
public:
    Game() : Application({.window = {.title = "Astra Run", .width = 1280, .height = 720}}) {}

protected:
    void onFixedUpdate(float dt) override { m_scene.fixedUpdate(dt); } // physics, gameplay
    void onUpdate(float dt) override { m_scene.update(dt); }           // menus, animation
    void onRender() override { /* sky, layers, tiles, m_scene.render(m_renderer, alpha), HUD */ }
};
```

**Input** goes through an @ref astra2d::ActionMap, so the game asks about actions, not keys. One
action can have several keyboard keys and gamepad buttons, and an axis can be a stick or two keys:

```cpp
m_actions.bindAxis("move_x", Key::A, Key::D)
    .bindAxis("move_x", Key::Left, Key::Right)
    .bindAxis("move_x", GamepadAxis::LeftX);
m_actions.bind("jump", Key::Space).bind("jump", GamepadButton::A);

// in the "player control" system, which runs in the fixed step:
const float moveX = m_actions.axis("move_x", input());
if (m_actions.isPressed("jump", input())) { /* ... */ } // since the previous step
```

Inside a fixed step `isPressed()` means "since the previous step", so a jump is never lost or
doubled at any frame rate. The Space that confirms a menu is handled in `onUpdate()`, and
`clearFixedStepPresses()` keeps it from also making the astronaut jump (see @ref core_guide).

## Entities and systems (scene, ecs)

Every game object is an entity in a @ref astra2d::Scene. It combines the engine's components
(`Transform`, `SpriteRenderer`, `Animator`, `RigidBody`, `Collider`, `TilemapCollider`,
`Interpolated`) with the game's own, which are plain structs: `Player`, `Enemy`, `Walker`,
`Coin`, `Spring`, `Checkpoint` and others. An enemy type is a combination of components. A crawler
is `Enemy` + `Walker`; a bat is `Enemy` + `Flyer` on a kinematic body.

```cpp
const Entity crawler = m_scene.createEntity("crawler", position);
registry.emplace<astra2d::SpriteRenderer>(crawler, astra2d::SpriteRenderer{.size = {16, 16}, .layer = 2});
registry.emplace<astra2d::Animator>(crawler, astra2d::Animator{.sheet = &m_sheet}).player.play(m_crawlAnimation);
registry.emplace<astra2d::RigidBody>(crawler, astra2d::RigidBody{.fixedRotation = true});
registry.emplace<astra2d::Collider>(crawler, astra2d::Collider{.halfExtents = {6, 4.5F}, .category = kEnemyGroup});
registry.emplace<Enemy>(crawler);
registry.emplace<Walker>(crawler, Walker{.speed = 32.0F});
```

The gameplay is a list of **systems** that the Scene runs in a fixed order, each a function over a
view of the components it needs:

```cpp
m_scene.addSystem(SystemPhase::FixedUpdate, "walkers", [this](Scene& scene, float) {
    scene.registry().view<Walker, astra2d::RigidBody, Transform>().each(
        [&](Walker& walker, astra2d::RigidBody& body, const Transform& t) { /* turn at walls */ });
});
```

| Phase | Systems |
|---|---|
| FixedUpdate | the engine's `physics`, then the game's `movers`, `player control`, `walkers`, `flyers`, `dangers` |
| Update | `player look` (which animation) |
| LateUpdate | the engine's `animation`, which advances every `Animator`, and `lifetime` |

Inside a view, an entity is removed with `scene.destroyLater()`, which is safe while iterating.
Effects need no system of their own. Dust and sparkles are Animators with `destroyWhenFinished`,
and a stomped enemy gets a `Lifetime` that removes it after a moment.

## Physics (physics, scene)

A @ref astra2d::PhysicsSystem connects the Scene to the physics world: an entity with a `Collider`
(and a `RigidBody` if it moves) becomes a body, and the system writes the simulated positions back to
`Transform`. The game uses all three body types:

- **dynamic**: the player and walking enemies. The controller sets the player's velocity directly
  (`fixedRotation`, no friction), and the solver keeps it out of walls and floors;
- **kinematic**: bats and moving platforms, which move only by the velocity the game gives them
  and push dynamic bodies;
- **static**: the level, as two `TilemapCollider`s: one for solid tiles and one for one-way tiles.

**Collision groups** decide what touches what. Each collider has a `category` bit and a `mask`.
The player drops through planks by leaving the platform group out of its mask for a moment, and
walks through enemies while it blinks after a hit:

```cpp
constexpr std::uint16_t kPlayerMask = astra2d::collisionGroups(
    kWorldGroup, kPlatformGroup, kEnemyGroup, kPickupGroup, kHazardGroup);
collider.mask = player.dropTime > 0.0F ? astra2d::withoutGroups(kPlayerMask, kPlatformGroup)
                                       : kPlayerMask;
```

**Events** reach the game through signals, so the systems never compare positions to find hits:

```cpp
m_physics.onCollisionBegin().connect([this](const astra2d::CollisionEvent& e) {
    // the contact normal tells a stomp (from above) from a hit
});
m_physics.onTriggerEnter().connect([this](const astra2d::TriggerContact& c) {
    // coins, hearts, springs, checkpoints, the rocket, spikes and lava are triggers
});
```

**Queries** answer the questions a platformer asks every step. `isGrounded()` decides whether the
player can jump. `rayCast()` lets a crawler look for a wall or the end of its floor, and finds the
platform the player stands on:

```cpp
const auto below = m_physics.rayCast(feet(), {0.0F, -3.0F}, kPlatformGroup);
if (below && registry.has<Mover>(below->entity)) { /* ride along at its velocity */ }
```

Planks, clouds and hover platforms are **one-way** (`Collider::oneWay`, or `OneWay` tiles): the
player jumps up through them and lands on top. Merged tile boxes have no internal edges, so the
player never catches on the seam between two tiles (see @ref physics_guide).

## Levels (scene)

Levels are made in the [Tiled](https://www.mapeditor.org) editor and loaded with
@ref astra2d::loadTiledMap(). The tileset marks tiles with a custom property `collision` (`solid` or
`oneway`), and @ref astra2d::makeTileGrid turns a layer into a grid for a `TilemapCollider`:

```cpp
const astra2d::TiledMap map = astra2d::loadTiledMap(file);
const astra2d::TiledTileLayer* ground = map.tileLayer("ground");
level.solid = astra2d::makeTileGrid(map, *ground, 16.0F, only(astra2d::TileCollision::Solid));
level.ground = astra2d::makeTileLayer(map, *ground, 16.0F); // what is drawn
for (const astra2d::TiledObject& object : map.objectLayer("objects")->objects) {
    // object.type is "player", "coin", "crawler", "goal", ...;
    // the thing starts at map.objectCenter(object, 16.0F)
}
level.name = map.properties.string("name", "Untitled");
```

Level 1 is a text map instead (one character per tile), parsed by the game into the same `Level`
data: the same @ref astra2d::TileLayer and @ref astra2d::TileGrid types, filled with `set()`. Both formats meet in one `startLevel()` that creates the colliders and entities.

## Drawing (renderer, assets)

An @ref astra2d::AssetCache loads every texture, sound and font once. All sprites come from
@ref astra2d::SpriteSheet frames, and each frame has a 1-texel extruded border so neighbours never
bleed in. A frame is drawn every frame in this order:

1. **The sky** in screen space: a gradient of quads, then textures tiled over the whole view with
   `drawTiled()` (a nebula and stars).
2. **Parallax layers**: each strip is drawn with a camera from
   `m_camera.withParallax({0.35F, 0.45F})`, so far layers move slower than the world.
3. **Tiles and sprites**: `drawTileLayer()` for the decoration and the ground, which draws only the
   tiles the camera sees; the rocket's exhaust smoke, a @ref astra2d::ParticleEmitter; then
   `m_scene.render(m_renderer, alpha)`, which draws every `SpriteRenderer` by layer at its
   interpolated position.
4. **The HUD** in screen space: centred, shadowed text with the vendored pixel font (a
   @ref astra2d::TextStyle with `anchor` and `shadowOffset`), sprites for hearts and coins, and
   panels.

Three engine parts move the camera, and none of their maths is in the game:

```cpp
m_camera.setPixelPerfect(224.0F);                    // whole pixels per texel, about 14 tiles high
m_follow.setBounds({0.0F, 0.0F}, m_level.size());    // a CameraFollow: never show outside the level
m_follow.update(player + lookAhead, dt, m_camera);   // eases after the player, at any frame rate
m_shake.apply(m_camera, m_follow.position());        // a CameraShake: trauma from stomps and hits
```

The look-ahead (28 units in the direction the astronaut faces) is the game's own choice.

## Sound (audio)

An @ref astra2d::AudioEngine plays the effects and the music loop; `pitchVariation` keeps repeated
sounds from getting tiring, and mute is one call:

```cpp
m_audio.playMusic(*m_sounds.music, 0.45F);
m_audio.play(*m_sounds.coin, {.volume = 0.45F, .pitchVariation = 0.06F});
m_audio.setMuted(!m_audio.muted());
```

## Debugging (debug)

F3 draws the physics world over the game with @ref astra2d::drawPhysicsDebug, showing bodies
coloured by type, one-way edges, triggers, contacts and velocities (see the second picture above).
F2 logs @ref astra2d::FrameStats, with FPS, frame times and the renderer's draw calls per frame.

## Assets

All art, sound and the Tiled levels are original (CC0) and made by
`scripts/generate_platformer_assets.py`, using only the Python standard library. The Tiled maps can
be opened and edited in Tiled after that. The font is Press Start 2P (SIL Open Font License).

<div class="astra-pager">
<span class="astra-pager-prev">← @ref debug_guide "Debug tools"</span>
<span class="astra-pager-next">Next: @ref evaluation "Evaluation" →</span>
</div>
