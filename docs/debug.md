# Debug tools {#debug_guide}

[TOC]

The @ref debug "debug" module helps to see what the engine does while a game runs: a debug view of
the physics world, frame statistics for the log, and an in-game overlay built with Dear ImGui that
shows a frame time graph, a CPU profiler and the scene's entities. All of them are opt-in: a game
decides on which key to show them, and nothing is drawn or measured otherwise.

<div class="astra-summary"><b>In short</b>
<ul>
<li>`drawPhysicsDebug()` draws the real collision shapes, body states, contacts and velocities with the game's camera.</li>
<li>`FrameStats` reports FPS, the min/mean/max frame time and draw calls once per interval.</li>
<li>`DebugOverlay` is an ImGui window with a frame graph, the profiler's call tree, the game's toggles and an entity list with a component inspector.</li>
<li>`ASTRA_PROFILE_SCOPE("name")` times a block of code; the main loop, scene systems, physics and renderer are already marked.</li>
<li>The examples use F1 for the overlay, F3 for the physics view and F2 for statistics.</li>
</ul></div>

## Physics debug view

@ref astra2d::drawPhysicsDebug draws the state of a @ref astra2d::PhysicsWorld on top of the game,
with the game's own camera:

```cpp
m_renderer.begin(m_camera);
m_scene.render(m_renderer, alpha);
if (m_showPhysics) { // toggled with F3
    astra2d::drawPhysicsDebug(m_renderer, m_physics.world(),
                              {.lineWidth = 0.75F, .contactSize = 4.0F, .normalLength = 8.0F});
}
m_renderer.end();
```

| Shown | How |
|---|---|
| Shapes | The real collision geometry (not the sprite): polygon outlines, circles with a spoke that turns with the body |
| Body state | Colour: static grey, kinematic blue, awake dynamic green, sleeping dynamic dark green; triggers yellow, one-way platforms orange |
| Disabled edges | Faint: the inner edges of merged tiles, which never give a contact normal, so nothing catches on seams |
| Bounding boxes | Optional: the boxes the broad phase compares |
| Contacts | A cross at each contact point and its normal, for touching contacts only |
| Velocities | An arrow from the centre of mass, `velocityScale` seconds of motion long |

@ref astra2d::PhysicsDebugSettings chooses what to show and how large, in world units: a game
measured in metres keeps the defaults, one measured in pixels scales them up.

The work is split in two. @ref astra2d::collectPhysicsDebug turns the world into a list of
coloured lines without touching OpenGL, so `tests/debug/test_debug.cpp` checks the geometry exactly
(corners, rotation, circle rims, colours, contact and arrow counts). drawPhysicsDebug() then draws
each line with the batched renderer, so a few thousand debug lines cost one or two draw calls.

<div class="astra-figures">
<div class="astra-figure"><img src="platformer_debug.png" alt="The platformer with F3, in the middle of a jump: the player's box and its velocity, the coins' triggers (yellow), a crawler and the merged boxes of the ground"/><span>The platformer with F3, in the middle of a jump: the player's box and its velocity, the coins' triggers (yellow), a crawler and the merged boxes of the ground</span></div>
<div class="astra-figure"><img src="physics_playground.png" alt="physics_playground with D: a bullet hitting the pyramid, a tower of boxes, bounding boxes of the static shapes"/><span>physics_playground with D: a bullet hitting the pyramid, a tower of boxes, bounding boxes of the static shapes</span></div>
</div>

## Frame statistics

@ref astra2d::FrameStats collects the frame times and @ref astra2d::RenderStats of every frame and
reports once per interval:

```cpp
// in onUpdate():  m_frameTime = dt;
// in onRender(), after everything is drawn:
if (m_logStats && m_frameStats.addFrame(m_frameTime, m_renderer.stats())) {
    ASTRA_INFO("{}", m_frameStats.summary());
}
m_renderer.resetStats();
```

```text
[GAME] [info] 60.0 FPS | frame 16.67 ms (min 16.34, max 17.01) | 4.0 draw calls, 509 quads per frame
```

The minimum and maximum matter as much as the mean: a single 40 ms frame among sixty 16 ms ones
hardly moves the average but is a visible stutter. @ref astra2d::FrameReport has the same numbers
for a HUD or the window title; the Sandbox shows the FPS and frame time in its HUD this way.

## Debug overlay

@ref astra2d::DebugOverlay draws two [Dear ImGui](https://github.com/ocornut/imgui) windows on top
of the game:

| Window | Part | Shows |
|---|---|---|
| Debug | Header | The frame rate in large type, green within the 60 FPS budget, yellow up to twice it, red beyond; mean, min and max frame time |
| | Time toolbar | Pause / Resume, *Step* (one fixed step while paused), and the time scale (0.25×, 0.5×, 1×, 2× or any value from 0.05× to 4×) |
| | Frame graph | The last 240 frames as bars coloured by the same budget, with the 60 and 30 FPS lines; hovering a bar shows its time. Below it, a bar of the frame split into its phases (update, render, swap buffers, ...) and the draw calls, quads and entities |
| | *Profiler* tab | The call tree of the CPU profiler (below): per scope the average time, the self time, the recent peak, the share of the frame and the calls per frame |
| | *Game* tab | The game's switches (`addToggle()`), e.g. the physics debug view and its parts, and the sections the game draws with ImGui (`addPanel()`) |
| Entities | | *Pick* (select an entity by clicking it in the game), *Move* (drag entities with the mouse), *Save scene*, the entities as the parent/child tree with a name filter, and an inspector for the selected entity |
| On the game | | Badges at the top while the game is paused, runs at another speed or a mouse tool is on, so they are seen with the windows collapsed; the selected entity's outline and name |

The windows use the engine's own theme: dark navy with the Astra2D blue as the accent, rounded
corners, and a scalable font. Sizes and text are scaled by the window's content scale, so the
overlay keeps its size on a HiDPI display instead of shrinking to a 13 px bitmap font.

A game creates it with its Application, feeds it each frame and draws it last:

```cpp
astra2d::DebugOverlay m_overlay{*this}; // a member of the Application

// once:
m_overlay.setScene(&m_scene);
m_overlay.setCamera(&m_camera); // for picking and moving entities with the mouse
m_overlay.setSceneFile(&m_serializer, "level.json"); // optional: a "Save scene" button
m_overlay.addToggle("Physics debug view (F3)", &m_showPhysics);
m_overlay.addPanel("Game", [this] { ImGui::SliderInt("Hearts", &m_hearts, 1, 3); });
m_overlay.inspector().addInspector<Walker>("Walker", [](Walker& walker) {
    ImGui::DragFloat("Speed", &walker.speed);
});

// in onUpdate():  if (input().isPressed(astra2d::Key::F1)) { m_overlay.toggle(); }
// in onRender(), in the world pass:
m_overlay.drawSelection(m_renderer, 1.0F); // outlines the selected entity
// at the end of onRender():
m_overlay.addFrame(m_renderer.stats());
m_renderer.resetStats();
m_overlay.draw();
```

**Time controls.** Pausing sets @ref astra2d::Application::setTimeScale to 0: no fixed steps run
and `onUpdate()` gets 0 seconds, so the world and its animations stand still while the game is
still drawn and the overlay stays usable. *Step* then runs exactly one fixed step, which is how a
collision or a jump can be followed frame by frame with the physics debug view on. The time scale
slows the game down or speeds it up; the scaled frame time is clamped like a real one, so a fast
forward cannot cause the "spiral of death" either. All of it lives in
@ref astra2d::FixedTimestep, which `tests/core/test_timestep.cpp` checks without a window. The
frame statistics use @ref astra2d::Application::frameTime, the real time, so they stay correct in
slow motion.

**Selection and picking.** The entity selected in the list is outlined in the game with a pulsing
box around its sprite (its size times the scale, rotated), or its collider if it has no sprite, so
it is easy to see which entity is which. The game calls `drawSelection()` in its world pass, with a
line width in its own world units.

It works the other way round too: after *Pick* in the Entities window, the entity under the cursor
is outlined faintly, and a click selects it. The list then expands its parents and scrolls to it,
and the inspector shows its components. As with a browser's element picker, one click picks and
ends the mode; a right click cancels. While picking, the overlay takes the mouse
(@ref astra2d::ImGuiLayer::setCaptureMouse), so the click does not also act in the game (no box
spawns in `physics_playground`). The entity under the cursor is the one whose box contains it and
that is drawn on top: the higher sprite layer, then the smaller box, so a coin in front of the
ground is found rather than the ground. That test is a function without OpenGL
(`tests/debug/test_entity_picking.cpp`).

**Moving.** *Move* keeps the mouse until a right click: pressing on an entity selects it, and
dragging moves it, held at the point where it was grabbed. The overlay sets the entity's world
position, converted into its parent's space if it has one, so a child of a rotated or scaled
parent follows the cursor exactly. A moved entity is drawn at its new place at once, without the
interpolated slide, and a RigidBody stops (velocity 0), so gravity does not pull a crate away from
the mouse; the PhysicsSystem takes the new Transform at its next step, as for any move by the game.
Combined with *Pause* this places bodies precisely before stepping the physics. The move is a
function without OpenGL (`tests/debug/test_entity_moving.cpp`).

**Saving.** With a @ref astra2d::SceneSerializer (`setSceneFile()`), *Save scene* writes the scene
to a JSON file with whatever the game has registered, e.g. after arranging entities with *Move*.
There is no load button: loading replaces the scene, and only the game knows what must happen
around that (the `scene` example suppresses its "coin collected" handler while it clears the old
scene, and loads with F9). `scene` and `physics_playground` have the button.

The **inspector** has widgets for every engine component (Name, Transform, SpriteRenderer with a
preview of its sprite, Animator, Lifetime, RigidBody, Collider, TilemapCollider, Relationship,
Interpolated). The registry stores components by type, not by name, so the game registers its own
with `addInspector<T>()`. Edits are ordinary changes to the components, so the systems pick them up
as if the game had made them: a moved Transform reaches the physics at its next step. The entity
list draws only the rows that are on screen, so a scene with thousands of entities stays cheap.

**Input.** @ref astra2d::ImGuiLayer installs its own GLFW callbacks in front of the Window's: every
event goes to ImGui, and a press goes on to the game's @ref astra2d::Input only while ImGui does not
want that device. A click on the overlay does not spawn a box in `physics_playground`, and typing in
the filter does not make the player jump. A release reaches the game only if its press did, so no
key stays held. The decision is a small class (`InputGate`) with its own tests.

Dear ImGui (v1.93.0, MIT) is vendored in `external/imgui` with its GLFW and OpenGL 3 backends. Its
headers are part of the engine's interface, so games can `#include <imgui.h>` for their panels;
@ref astra2d::ImGuiLayer can also be used on its own, for a tool without the overlay. Setting the
environment variable `ASTRA2D_DEBUG_OVERLAY=1` shows the overlay from the start, which the CI smoke
test uses to run its drawing code.

## CPU profiler

@ref astra2d::Profiler measures how long named blocks of code take per frame and keeps them as a
call tree. A block is marked with one line:

```cpp
void Game::moveEnemies()
{
    ASTRA_PROFILE_SCOPE("move enemies");
    ...
}
```

The engine marks its own work, so the tree has useful content without any game code:

```text
frame
├─ poll events, dispatch events
├─ fixed update            (once per fixed step: calls = steps this frame)
│  └─ physics              (a scene system, by its name)
│     ├─ sync, world step ─ broad phase, narrow phase, solver
│     └─ write back, events
├─ update                  (the scene's Update and LateUpdate systems by name)
├─ render ─ scene render, renderer flush
└─ swap buffers            (includes waiting for vsync)
```

- A scope is identified by its name **and its parent**, so the same scope reached from two places
  gives two rows, and a scope entered several times in one frame (a loop, two fixed steps) is one
  row with a call count.
- The overlay shows the time averaged over recent frames (an exponential moving average), because
  per-frame numbers change too fast to read; *Freeze* stops them changing.
- An average hides spikes: one 40 ms hitch every few seconds hardly moves it. So each scope also
  has a **peak**, the longest time within the last 60 to 120 frames (two blocks of 60 frames: the
  maximum of the current and the previous block), which keeps a spike visible for one to two
  seconds.
- **Self** is a scope's time minus its children's: work done directly in it, or in code that has no
  scope of its own.
- Recording is off unless the overlay is visible. A scope then costs one branch, so the markers
  stay in release builds and do not change the benchmarks.
- Only scopes inside a frame (between `beginFrame()` and `endFrame()`, which `Application::run()`
  calls) are recorded, so headless code such as the physics benchmark is never measured.

Times include the scope's children. In the platformer, most of the self time of "render" is the
game's direct `Renderer2D` calls (background layers, tiles, the HUD), and "swap buffers" is mostly
waiting for vsync: a frame that has to wait there is within its budget. `tests/core/test_profiler.cpp`
checks the tree, the call counts, the averages and the peaks with a fake clock.

## In the examples

| Example | Overlay | Physics view | Statistics in the log |
|---|---|---|---|
| `platformer` | F1 (hearts, level select, the game's components) | F3 | F2 |
| `coin_collector` | F1 (lives, time left) | F3 | F2 |
| `physics_playground` | F1 (gravity, solver iterations, broad phase, step statistics) | D (with bounding boxes) | F2 |
| `scene` | F1 (the hierarchy; save, load, rebuild) | | |
| `Sandbox` | F1 (batching, zoom, camera rotation) | | always in the HUD |

<div class="astra-pager">
<span class="astra-pager-prev">← @ref audio_guide "Audio"</span>
<span class="astra-pager-next">Next: @ref platformer_guide "Example game" →</span>
</div>
