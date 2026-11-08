# Core {#core_guide}

[TOC]

The @ref core "core" module turns an empty `main()` into a running game: it opens the window,
runs the main loop, delivers input and splits time into fixed simulation steps.

<div class="astra-summary"><b>In short</b>
<ul>
<li>`Application::run()` owns the loop; a game overrides `onStart()`, `onFixedUpdate()`, `onUpdate()` and `onRender()`.</li>
<li>The simulation advances in fixed steps (1/60 s by default), zero or more per frame; drawing interpolates between them.</li>
<li>Read one-frame input such as `isPressed()` in `onUpdate()`, not in `onFixedUpdate()`.</li>
<li>`ActionMap` binds named actions to keys and gamepads; `EventDispatcher` lets systems talk without knowing each other.</li>
</ul></div>

## The main loop

@ref astra2d::Application::run "Application::run()" repeats the following until the window is
closed or @ref astra2d::Application::quit "quit()" is called:

@dot
digraph frame {
    rankdir=TB;
    nodesep=0.5;
    ranksep=0.28;
    node [shape=box, style="rounded,filled", fontname="Helvetica,Arial,sans-serif", fontsize=10,
          color="#2a7fae", fillcolor="#eaf4fa", width=2.4];
    edge [color="#5a7b91", arrowsize=0.7, fontname="Helvetica,Arial,sans-serif", fontsize=9,
          fontcolor="#5a7b91"];

    start  [label="onStart()", shape=ellipse, width=1.2];
    begin  [label="poll events and gamepads  ·  dispatch queued events"];
    resize [label="onResize(w, h)  if the size changed", style="rounded,dashed"];
    fixed  [label="onFixedUpdate(step)", fillcolor="#2a7fae", fontcolor="white"];
    update [label="onUpdate(frameTime)"];
    render [label="onRender()  ·  swap buffers"];
    stop   [label="onShutdown()", shape=ellipse, width=1.2];

    start -> begin -> resize -> fixed;
    fixed:w -> fixed:w [label="0..n times  "];
    fixed -> update -> render;
    render:e -> begin:e [label="  next frame", style=dashed, constraint=false];
    render -> stop  [label="  window closed / quit()", style=dotted];
}
@enddot

| Hook | Called | Use it for |
|---|---|---|
| `onStart()` | once, before the first frame | loading resources |
| `onFixedUpdate(step)` | 0 or more times per frame, always with the same `step` | physics, movement, game rules |
| `onUpdate(frameTime)` | once per frame, with the real elapsed time | one-shot input, animation, UI |
| `onRender()` | once per frame | drawing |
| `onResize(w, h)` | when the framebuffer size changes | updating the camera's aspect ratio |
| `onShutdown()` | once, after the last frame | releasing resources |

While the window is minimized, the loop sleeps until the next event instead of spinning, and the
time spent minimized is not passed to the simulation. Input received while minimized (such as the
click that restores the window) is kept and reported in the first frame afterwards.

`onUpdate()` receives the same clamped frame time as the simulation (at most
`ApplicationSettings::maxFrameTime`, 0.25 s by default), so a stall or a breakpoint does not make
animations and timers jump.

## Fixed timestep

A physics simulation is only stable and reproducible if it advances in equal steps. The display, on
the other hand, runs at whatever rate the monitor and GPU allow. @ref astra2d::FixedTimestep
decouples the two with an accumulator, following Glenn Fiedler's *Fix Your Timestep!*.

Each frame, the real frame time \f$\Delta t_{frame}\f$ is added to the accumulator \f$A\f$, and
whole steps of length \f$h\f$ are taken out of it:

\f[
A \leftarrow A + \min(\Delta t_{frame},\ \Delta t_{max}), \qquad
n = \left\lfloor \frac{A}{h} \right\rfloor, \qquad
A \leftarrow A - n\,h
\f]

`onFixedUpdate()` then runs \f$n\f$ times. The remainder \f$A < h\f$ carries over to the next
frame, so over time the simulation advances by exactly as much time as really passed.

<div class="tabbed">

- <b class="tab-title">Fast display (144 Hz)</b>
  With \f$h = 1/60\f$ s and frames of about 6.9 ms, most frames run **0** steps and every
  second or third frame runs **1**. Rendering uses the interpolation factor (below) to stay smooth
  between steps.
- <b class="tab-title">Slow display (30 Hz)</b>
  Frames of about 33 ms run **2** steps each, so the game still simulates at 60 Hz.
- <b class="tab-title">A long stall</b>
  A 2-second freeze (e.g. dragging the window) would ask for 120 steps at once. That would make the
  next frame slow too, and the one after it slower (the "spiral of death"). To prevent this, the
  frame time is clamped to \f$\Delta t_{max}\f$ = 0.25 s, so at most 15 steps run and the game
  briefly runs slower than real time instead. \f$\Delta t_{max}\f$ is set with
  `ApplicationSettings::maxFrameTime` and is never shorter than one step.

</div>

### Smooth rendering with interpolation

After the steps, the accumulator holds a fraction of a step that has not been simulated yet.
@ref astra2d::Application::interpolationAlpha "interpolationAlpha()" returns it as
\f$\alpha = A / h \in [0, 1)\f$. Keeping the previous and current position of an object and
drawing it at

\f[
x_{render} = x_{prev} + \alpha\,(x_{curr} - x_{prev})
\f]

removes the stutter that would otherwise appear when the display rate is not a multiple of the
simulation rate. The sandbox example does exactly this.

## Input

@ref astra2d::Input answers three questions for every key, mouse button and gamepad button:

| Query | True when | Typical use |
|---|---|---|
| @ref astra2d::Input::isPressed "isPressed()" | the button went down this frame | jump, shoot, open a menu |
| @ref astra2d::Input::isHeld "isHeld()" | the button is down | walk, aim, charge |
| @ref astra2d::Input::isReleased "isReleased()" | the button went up this frame | release a charged attack |

For a key pressed in frame 2 and released in frame 4:

| Frame | 1 | 2 | 3 | 4 | 5 |
|---|:-:|:-:|:-:|:-:|:-:|
| `isPressed` |   | ✔ |   |   |   |
| `isHeld`    |   | ✔ | ✔ |   |   |
| `isReleased`|   |   |   | ✔ |   |

States are updated from events, not by comparing two snapshots. A key that is pressed and
released between two frames therefore still reports both `isPressed()` and `isReleased()` in the
next frame, so very short taps are not lost.

@warning `isPressed()` and `isReleased()` are true for one **frame**, while `onFixedUpdate()` runs
zero or more times per frame. Read them in `onUpdate()` and store the intent (for example
`m_jumpRequested = true`) for the next fixed update. Reading them in `onFixedUpdate()` would miss
presses on frames with zero steps and repeat them on frames with two. `isHeld()` is safe in both.

Mouse position is in framebuffer pixels with the origin in the top-left corner, the same units as
the viewport and `onResize()`. On HiDPI displays GLFW reports the cursor in smaller screen
coordinates; `Window` scales it, so a click can be compared with the framebuffer size directly.
`mouseDelta()` and `scrollDelta()` hold the movement since the previous frame.

When the window loses focus, all keys and buttons are released, so a key held while switching
windows does not stay stuck. A key that is still down when focus returns is reported again only
once it is pressed again.

### Why Input does not depend on GLFW

@ref astra2d::Window installs the GLFW callbacks and forwards each event to `Input::onKey()`,
`onMouseButton()` and so on. Because `Input` itself knows nothing about GLFW, its behaviour is
fully covered by unit tests that feed it synthetic events (`tests/core/test_input.cpp`). The @ref
astra2d::Key and @ref astra2d::MouseButton values equal the GLFW codes, which is checked at compile
time, so no translation table is needed.

### Gamepads

Up to four gamepads are supported through GLFW's gamepad API, which maps hundreds of controllers to
one standard (Xbox-style) layout. Gamepads are *polled* once per frame rather than delivered as
events, so `Input` finds presses and releases by comparing each poll with the previous one:

```cpp
if (input().isPressed(astra2d::GamepadButton::A)) { jump(); }
const glm::vec2 move = input().leftStick();          // dead zone applied, length <= 1
const float brake = input().axis(astra2d::GamepadAxis::LeftTrigger);  // 0 to 1
```

Stick Y is flipped so that up is positive, matching the y-up world. Small stick deflections inside
the **dead zone** (`setDeadZone()`, 0.15 by default) read as 0. The dead zone is *radial*: the
distance of the stick from the centre decides, not each axis separately, so diagonal movement is not
cut off, and the remaining range is rescaled to start at 0, so there is no jump at its edge.

## Action mapping

Game code should ask "does the player want to jump?", not "is Space down?".
@ref astra2d::ActionMap binds named actions to any number of keys, mouse buttons and gamepad
inputs:

```cpp
astra2d::ActionMap actions;
actions.bind("jump", Key::Space).bind("jump", GamepadButton::A);
actions.bindAxis("move_x", Key::A, Key::D)          // -1 / +1
       .bindAxis("move_x", GamepadAxis::LeftX);    // analog

if (actions.isPressed("jump", input())) { ... }
velocity.x = actions.axis("move_x", input()) * speed;
```

| Kind | Value | Rule with several bindings |
|---|---|---|
| Button action | pressed / held / released | held while *any* binding is held; pressed when it becomes held, released when the last binding is let go |
| Axis action | -1 to 1 | the binding with the largest magnitude wins |

<div class="astra-figures single">
<div class="astra-figure"><img src="input.png" alt="<code>examples/input</code>: the move axis and jump action from keyboard or gamepad, rebinding with R"/><span><code>examples/input</code>: the move axis and jump action from keyboard or gamepad, rebinding with R</span></div>
</div>

Each map reads one gamepad, so local multiplayer uses one map per player. Asking for an action that
was never bound fails an assertion in debug builds: such a name is almost always a typo, which would
otherwise silently do nothing. The class is plain logic over Input and is unit-tested
(`tests/core/test_actions.cpp`).

## Events

Systems that should not know each other communicate through events. @ref astra2d::EventDispatcher
is a type-safe publish/subscribe mechanism: an event is any struct, and handlers subscribe to its
type.

```cpp
struct CoinCollected { int value; };

auto subscription = events().subscribe<CoinCollected>([&](const CoinCollected& e) {
    m_score += e.value;
});
events().publish(CoinCollected{10});   // handlers run now
events().enqueue(CoinCollected{5});    // handlers run at a fixed point of the next frame
```

| Property | How |
|---|---|
| Lifetime | a handler stays registered while its `Subscription` exists; destroying the subscription unsubscribes (RAII), also after the dispatcher is gone |
| Re-entrancy | handlers may publish, subscribe and unsubscribe during dispatch; new handlers start with the next event |
| Deferred events | `enqueue()` stores a copy; `dispatchQueued()` delivers them in order, events queued meanwhile wait for the next call |
| Order | handlers run in subscription order |

Application owns a dispatcher (`events()`) and calls `dispatchQueued()` once per frame, right after
polling the window. The engine publishes `WindowResizeEvent`, `WindowFocusEvent` and
`GamepadConnectionEvent` on it. The window events are queued during polling, so their handlers run at
that defined point of the frame rather than inside GLFW's callbacks.

## Finding files

A game's working directory depends on how it was started (a launcher, a desktop shortcut, a
terminal somewhere else), so paths relative to it break. Paths compiled in at build time point
into the source tree, which players do not have. @ref astra2d::executableDirectory returns the
directory of the running executable instead, and @ref astra2d::findDirectory picks the first
existing directory from a list:

```cpp
#include <astra2d/core/paths.hpp>

const auto assets = astra2d::findDirectory({
    astra2d::executableDirectory() / "assets", // shipped: assets/ next to the executable
    GAME_SOURCE_ASSETS_DIR,                    // development: the source tree, set by meson.build
});
```

If none exists, the exception lists every place that was tried. Installing the assets next to the
executable (`install_subdir('assets', install_dir : get_option('bindir'))` in Meson) makes the
first candidate work for a shipped game.

## Logging

Two loggers show where a message came from:

```cpp
ASTRA_CORE_INFO("OpenGL {}", version);   // engine code   -> [ASTRA] [info] ...
ASTRA_WARN("Level {} has no exit", name); // game code     -> [GAME] [warning] ...
```

Both use the `{}` format syntax, are created on first use (no setup call, also in tests), and write
to stderr so stdout stays free for program output such as benchmark CSV. Messages at error level
and above are flushed immediately. The default level is `trace` in debug builds and `info` in
release builds; use @ref astra2d::log::setLevel to change it. The loggers are not registered in
spdlog's global registry, so user code may create its own spdlog loggers (even one named `GAME`)
or call `spdlog::drop_all()` without affecting them.

## Assertions

`ASTRA_ASSERT` checks for programmer errors:

```cpp
ASTRA_ASSERT(index < size, "index {} out of range ({})", index, size);
```

On failure it logs the condition, file, line and message, then aborts, so a debugger stops at the
failing line. Assertions are compiled out in release builds (`NDEBUG`) unless `ASTRA_ENABLE_ASSERTS`
is defined, so the condition must not have side effects (it is still type-checked). The message uses
the same formatter as the log macros. Errors the program can recover from, such as a missing file
or invalid settings like a non-positive fixed step, throw exceptions instead.

<div class="astra-pager">
<span class="astra-pager-prev">← @ref architecture "Architecture"</span>
<span class="astra-pager-next">Next: @ref renderer_guide "Renderer" →</span>
</div>
