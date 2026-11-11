# Comparison with raylib and Godot {#comparison}

[TOC]

Astra2D sits between two well-known ways of making a 2D game. **raylib** is a C library of
functions: the game writes its own loop and its own game objects, and draws in immediate mode.
**Godot** is a complete engine with an editor: the game is a tree of nodes, each with a script, and
the engine owns the loop. Astra2D is a C++ library like raylib (the game is a program that links
it), but it brings the structure of an engine: a fixed-step main loop, an entity component system,
physics with collision events, and a scene that draws itself.

<div class="astra-summary"><b>In short</b>
<ul>
<li>**raylib** is a library the game drives: its own loop, objects and collision code.</li>
<li>**Godot** is an engine with an editor: it owns the loop, and the game is a tree of scripted nodes.</li>
<li>**Astra2D** is a library like raylib, but brings an engine's structure: fixed-step loop, ECS, physics and a scene.</li>
</ul></div>

## The same task in three APIs

A character that runs left and right and jumps when it stands on the ground.

**raylib** (C). The loop is the game's; there is no physics, so the game integrates gravity and
resolves the collision with the floor itself:

```c
InitWindow(1280, 720, "demo");
SetTargetFPS(60);
Texture2D hero = LoadTexture("hero.png");
Vector2 position = {100, 300}, velocity = {0, 0};
while (!WindowShouldClose()) {
    const float dt = GetFrameTime();                 // variable frame time
    velocity.x = 200.0f * (IsKeyDown(KEY_RIGHT) - IsKeyDown(KEY_LEFT));
    velocity.y += 1200.0f * dt;                      // y points down
    const bool onFloor = position.y >= 300;
    if (onFloor) { position.y = 300; velocity.y = 0; }
    if (onFloor && IsKeyPressed(KEY_SPACE)) velocity.y = -400.0f;
    position.x += velocity.x * dt;
    position.y += velocity.y * dt;
    BeginDrawing();
    ClearBackground(RAYWHITE);
    DrawTextureV(hero, position, WHITE);
    EndDrawing();
}
```

**Godot 4** (GDScript, attached to a `CharacterBody2D` node made in the editor, with a sprite and a
collision shape as child nodes). The engine owns the loop and calls the script at the physics rate;
`move_and_slide()` moves the body and resolves its collisions:

```gdscript
extends CharacterBody2D

var gravity = ProjectSettings.get_setting("physics/2d/default_gravity")

func _physics_process(delta):
    if not is_on_floor():
        velocity.y += gravity * delta
    if Input.is_action_just_pressed("jump") and is_on_floor():
        velocity.y = -400.0
    velocity.x = Input.get_axis("move_left", "move_right") * 200.0
    move_and_slide()
```

**Astra2D** (C++). The character is an entity with components; a system run by the Scene at the
fixed timestep sets its velocity, and the PhysicsSystem moves it and resolves collisions:

```cpp
// Once, when the level is made:
const auto hero = m_scene.createEntity("hero", {100.0F, 300.0F});
auto& registry = m_scene.registry();
registry.emplace<astra2d::SpriteRenderer>(hero, astra2d::SpriteRenderer{.sprite = heroSprite, .size = {16.0F, 16.0F}});
registry.emplace<astra2d::RigidBody>(hero, astra2d::RigidBody{.fixedRotation = true});
registry.emplace<astra2d::Collider>(hero, astra2d::Collider{.halfExtents = {5.0F, 7.0F}, .friction = 0.0F});

// A system, called at the fixed timestep after the physics step:
m_scene.addSystem(astra2d::SystemPhase::FixedUpdate, "hero", [this, hero](astra2d::Scene& scene, float) {
    auto& body = scene.registry().get<astra2d::RigidBody>(hero);
    body.velocity.x = 200.0F * m_actions.axis("move_x", input());
    if (m_actions.isPressed("jump", input()) && m_physics.isGrounded(hero)) {
        body.velocity.y = 400.0F;                    // y points up; gravity is the world's
    }
});
```

The three are about the same length, but they divide the work differently. In raylib the game does
everything after drawing and input: integration, collision, game objects. In Godot the game
describes objects in an editor and scripts only their behaviour. In Astra2D the game describes
objects in code as components and adds behaviour as systems; the engine does the loop, the
physics and the drawing.

## Side by side

| | raylib | Godot 4 | Astra2D |
|---|---|---|---|
| Form | C library of functions | engine with an editor | C++20 library |
| Who owns the loop | the game | the engine | the engine (`Application::run()`), the game overrides hooks |
| Timestep | variable frame time | fixed physics rate, variable frame rate | fixed step with interpolated drawing |
| Game objects | the game's own structs | tree of nodes with scripts (inheritance) | entities with components in sparse sets; systems |
| Physics | none built in | built in: bodies, areas, character controller | built in: own impulse solver, triggers, one-way platforms, events |
| Collision response for a character | the game's code | `move_and_slide()` | a dynamic body with fixed rotation; the game sets its velocity |
| Rendering | immediate mode, batched internally | scene renderer | batched quads; the Scene draws sprite components by layer |
| Input | keys and buttons | named actions (Input Map) | named actions (`ActionMap`), keyboard and gamepad |
| Levels | the game's own | editor (TileMap), scenes | text maps, Tiled JSON, JSON scene files |
| Audio | built in (raudio) | built in | built in (`AudioEngine`, miniaudio) |
| Tools | none | editor, debugger, profiler | debug draw, frame statistics, an in-game ImGui overlay (profiler, entity inspector, time controls), hot reloading of assets |
| Testing | the game's own | engine and editor | headless modules unit-tested; sanitizers, Valgrind, CI |

## What the comparison shows

- **Structure versus freedom.** raylib leaves every decision to the game, which is easy to start
  and hard to grow; Godot decides almost everything, with an editor in the centre. Astra2D gives a
  game the structure of an engine (loop, ECS, physics, scene) but stays a library that the game's
  own code drives, so there is nothing to learn beyond the C++ API.
- **Data-oriented game objects.** Godot's node tree is easy to understand and edit, and each node
  carries its own behaviour. Astra2D's components are plain data in packed arrays and the behaviour
  is in systems that iterate over them; evaluation E2 measures what this layout gains
  (@ref evaluation).
- **Character control.** Godot moves a character body kinematically and lets `move_and_slide()`
  resolve the collisions. Astra2D uses the general rigid-body solver for the character too (a
  dynamic body that cannot rotate and has no friction), so characters, crates and enemies all
  follow the same rules; the platformer adds coyote time and jump buffering on top
  (@ref platformer_guide).
- **No editor.** The largest gap to Godot. Levels come from text maps or the Tiled editor and
  scenes from JSON files. The in-game overlay covers the basic editing a running game needs
  (select an entity by clicking it, move it, change its components, save the scene), but there is
  no asset browser, undo or separate edit mode.
- **Debug tools in the game, not beside it.** Godot shows its profiler, monitors and the running
  game's scene tree in the editor, connected to the game over the debugger; Unity does the same with
  its Profiler, Stats window and Inspector. Astra2D has no editor, so these tools are drawn in the
  game itself: the F1 overlay with Dear ImGui, the library that many studio engines use for their
  internal debug menus. This is closer to Unreal's in-game tools (the `stat` commands, the Gameplay
  Debugger) than to an editor. Its profiler works like Tracy's `ZoneScoped` or Unreal's
  `SCOPE_CYCLE_COUNTER`: a named scope measured from its start to its end and gathered into a
  per-frame call tree. raylib has none of this built in (`DrawFPS()` only) and leaves it to add-ons
  such as rlImGui.
- **Measured and tested.** Astra2D's own systems are compared against naive baselines (batching,
  ECS layout, broad phase) and unit-tested headless, which is possible because the ECS, the physics
  and the audio do not depend on OpenGL.

<div class="astra-pager">
<span class="astra-pager-prev">← @ref evaluation "Evaluation"</span>
<span class="astra-pager-next">Next: <a href="topics.html">API reference</a> →</span>
</div>
