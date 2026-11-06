# Core: main loop, timestep and input {#core_guide}

[TOC]

The @ref core "core" module turns an empty `main()` into a running game: it opens the window,
runs the main loop, delivers input and splits time into fixed simulation steps.

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
    begin  [label="poll events  (starts a new Input frame)"];
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

@ref astra2d::Input answers three questions for every key and mouse button:

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
fully covered by unit tests that feed it synthetic events (`tests/test_input.cpp`). The @ref
astra2d::Key and @ref astra2d::MouseButton values equal the GLFW codes, which is checked at compile
time, so no translation table is needed.

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
