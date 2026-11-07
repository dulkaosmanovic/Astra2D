# Debug tools: physics view and frame statistics {#debug_guide}

[TOC]

The @ref debug "debug" module helps to see what the engine does while a game runs: a debug view of
the physics world and frame statistics for the log. Both are opt-in: a game decides on which key to
show them, and nothing is drawn or measured otherwise.

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
coloured lines without touching OpenGL, so `tests/test_debug.cpp` checks the geometry exactly
(corners, rotation, circle rims, colours, contact and arrow counts). drawPhysicsDebug() then draws
each line with the batched renderer, so a few thousand debug lines cost one or two draw calls.

![The platformer with F3: the player's box and velocity (cyan), coin and lava triggers (yellow), the one-way plank, the crawler's contacts (red)](platformer_debug.png)

![physics_playground with D: a bullet hitting the pyramid, a tower of boxes, bounding boxes of the static shapes](physics_playground.png)

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

## In the examples

| Example | Physics view | Statistics in the log |
|---|---|---|
| `platformer` | F3 | F2 |
| `coin_collector` | F3 | F2 |
| `physics_playground` | D (with bounding boxes) | F2 |
| `Sandbox` | | always in the HUD |
