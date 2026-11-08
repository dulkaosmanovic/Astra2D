# Architecture {#architecture}

[TOC]

<div class="astra-summary"><b>In short</b>
<ul>
<li>Eight modules in three layers. A module uses modules in its own or a lower layer, never a higher one.</li>
<li>`ecs`, `physics` and `audio` do not touch OpenGL, so they are tested and benchmarked without a GPU.</li>
<li>Games include only the public headers in `include/astra2d/`.</li>
</ul></div>

## Layers

The engine is split into modules arranged in layers. A module may use modules in the same or a
lower layer, never a higher one. Games use only the public headers in `include/astra2d/`.

@dot
digraph layers {
    rankdir=TB;
    nodesep=0.25;
    ranksep=0.35;
    node [shape=box, style="rounded,filled", fontname="Helvetica,Arial,sans-serif", fontsize=11,
          color="#2a7fae", fillcolor="#eaf4fa", width=1.6];
    edge [color="#5a7b91", arrowsize=0.7];

    game     [label="Games and demos\nexamples/", fillcolor="#2a7fae", fontcolor="white"];

    subgraph cluster_high {
        label="High level"; fontname="Helvetica,Arial,sans-serif"; fontsize=10; fontcolor="#5a7b91";
        style="rounded,dashed"; color="#9fbfd3";
        scene [label="scene", fillcolor="#1a2f55", fontcolor="white", URL="\ref scene"];
        debug [label="debug", fillcolor="#1a2f55", fontcolor="white", URL="\ref debug"];
    }
    subgraph cluster_systems {
        label="Systems"; fontname="Helvetica,Arial,sans-serif"; fontsize=10; fontcolor="#5a7b91";
        style="rounded,dashed"; color="#9fbfd3";
        ecs      [label="ecs", fillcolor="#1a2f55", fontcolor="white", URL="\ref ecs"];
        physics  [label="physics", fillcolor="#1a2f55", fontcolor="white", URL="\ref physics"];
        renderer [label="renderer", fillcolor="#1a2f55", fontcolor="white", URL="\ref renderer"];
        assets   [label="assets", fillcolor="#1a2f55", fontcolor="white", URL="\ref assets"];
        audio    [label="audio", fillcolor="#1a2f55", fontcolor="white", URL="\ref audio"];
    }
    subgraph cluster_base {
        label="Foundation"; fontname="Helvetica,Arial,sans-serif"; fontsize=10; fontcolor="#5a7b91";
        style="rounded,dashed"; color="#9fbfd3";
        core [label="core", fillcolor="#1a2f55", fontcolor="white", URL="\ref core"];
    }

    game -> scene; game -> debug; game -> audio;
    scene -> ecs; scene -> physics; scene -> renderer; scene -> assets;
    debug -> renderer; debug -> physics;
    assets -> renderer;
    renderer -> core; assets -> core; ecs -> core; audio -> core; physics -> core;
}
@enddot

| Module | Responsibility | Uses OpenGL | Status |
|---|---|---|---|
| @ref core "core" | Application, main loop, window, input, timestep, logging, assertions | yes | ready |
| @ref assets "assets" | Load each texture and shader file once | yes | ready |
| @ref renderer "renderer" | Shaders, textures, batched quads, camera, sprites, animation, debug lines | yes | ready |
| @ref ecs "ecs" | Entities, sparse-set component storage, views, signals | no | ready |
| @ref physics "physics" | Bodies, shapes, broad and narrow phase, impulse solver, tile maps | no | ready |
| @ref audio "audio" | Sound effects and music: decoding, voices, buses, headless mixing | no | ready |
| @ref scene "scene" | Scene with systems, components, hierarchy, sprite rendering, scene files, Tiled maps | rendering only | ready |
| @ref debug "debug" | Physics debug view, frame statistics | drawing only | ready |

There is no separate math module (the plan had one): GLM's vector types are used directly
throughout the API, and the 2D geometry the engine needs (rotations, poses, bounding boxes, convex
polygons) lives in `physics/geometry.hpp`, the only place that uses it beyond GLM.

`ecs`, `physics` and `audio` do not depend on OpenGL. They can be unit-tested and benchmarked
headless, which is what the evaluation chapter relies on.

## One frame through the modules

The layers say who may use whom; this is how data moves through them in one frame of a game such
as the platformer. The fixed step can run zero or more times per frame (see @ref core_guide);
everything in it is deterministic.

@dot
digraph frame {
    rankdir=LR;
    nodesep=0.3;
    ranksep=0.45;
    node [shape=box, style="rounded,filled", fontname="Helvetica,Arial,sans-serif", fontsize=10,
          color="#2a7fae", fillcolor="#eaf4fa"];
    edge [color="#5a7b91", arrowsize=0.6, fontname="Helvetica,Arial,sans-serif", fontsize=9,
          fontcolor="#5a7b91"];

    window  [label="Window\npoll events", URL="\ref astra2d::Window"];
    input   [label="Input +\nActionMap", URL="\ref astra2d::ActionMap"];
    subgraph cluster_fixed {
        label="fixed step (Scene::fixedUpdate)"; fontname="Helvetica,Arial,sans-serif";
        fontsize=10; fontcolor="#5a7b91"; style="rounded,dashed"; color="#9fbfd3";
        physics [label="PhysicsSystem\n(sync, step,\nwrite back)", URL="\ref astra2d::PhysicsSystem"];
        world   [label="PhysicsWorld\nbroad, narrow,\nsolver", URL="\ref astra2d::PhysicsWorld"];
        game    [label="game systems\n(player, enemies)", fillcolor="#2a7fae", fontcolor="white"];
    }
    components [label="Registry\nTransform, RigidBody,\nCollider, ...", URL="\ref astra2d::Registry"];
    subgraph cluster_frame {
        label="frame (update, render)"; fontname="Helvetica,Arial,sans-serif"; fontsize=10;
        fontcolor="#5a7b91"; style="rounded,dashed"; color="#9fbfd3";
        animation [label="Animator\n(LateUpdate)", URL="\ref astra2d::Animator"];
        render    [label="Scene::render\nby layer,\ninterpolated", URL="\ref astra2d::Scene"];
        batch     [label="Renderer2D\nbatches", URL="\ref astra2d::Renderer2D"];
    }
    gl    [label="OpenGL", shape=cylinder];
    audio [label="AudioEngine", URL="\ref astra2d::AudioEngine"];

    window -> input [label="keys, pads"];
    input -> game [label="actions"];
    game -> components [label="velocities"];
    components -> physics [label="changes"];
    physics -> world;
    world -> physics [label="poses, events"];
    physics -> components [label="Transform"];
    physics -> game [label="collision /\ntrigger signals"];
    game -> audio [label="play"];
    components -> animation;
    animation -> components [label="frame"];
    components -> render [label="sprites"];
    render -> batch [label="quads"];
    batch -> gl [label="few draw calls"];
}
@enddot

- **Input** is polled once per frame and read through named actions, so keyboard and gamepad
  drive the same code.
- **Gameplay** sets velocities on components and reacts to the physics' signals; it never calls
  OpenGL and does not own the bodies.
- **PhysicsSystem** is the bridge between components and the physics world: it applies what the
  game changed, steps the world and writes the poses back to `Transform`.
- **Rendering** reads components only: the Scene sorts the sprites by layer, blends between the
  last two fixed steps, and the batch renderer turns thousands of sprites into a few draw calls.

## Source layout

```text
include/astra2d/<module>/   public headers: the engine API
src/<module>/               implementation
tests/                      unit tests (GoogleTest), one file per class or feature
examples/                   example programs, from hello_window to the platformer game
examples/assets/            textures, shaders, fonts, sounds and levels used by the examples
docs/                       Doxygen configuration, these pages and the theme
external/                   vendored code (GLAD, stb_image, stb_truetype, miniaudio, doxygen-awesome-css)
benchmarks/                 headless benchmark programs; results/ holds the CSV files and summary
subprojects/                Meson wraps for GLFW, GLM, spdlog, nlohmann/json and GoogleTest
```

## Dependencies

| Library | Used for |
|---|---|
| GLFW | Window, OpenGL context, input events |
| GLAD | Loading OpenGL 3.3 core functions |
| GLM | Vectors and matrices |
| spdlog | Logging (wrapped by @ref astra2d::log) |
| nlohmann/json | Scene files (@ref astra2d::SceneSerializer) and Tiled maps (@ref astra2d::loadTiledMap); header-only |
| stb_image | Decoding PNG/JPEG/BMP/TGA textures (vendored, private to the engine library) |
| stb_truetype | Baking TrueType fonts into atlases (vendored, private to the engine library) |
| miniaudio | Decoding, mixing and playing sound (vendored, private to the engine library) |
| GoogleTest | Unit tests |

Meson uses system packages when they are installed and falls back to the wraps in
`subprojects/` otherwise, so the project builds on a machine with only a compiler, Meson and Ninja.
Third-party headers are included as system headers, so their warnings do not affect the engine's
`-Dwerror=true` build.

The engine is exported as the Meson dependency `astra2d` (`meson.override_dependency`), so a game
can use the repository as a subproject with `dependency('astra2d')`. GoogleTest is only needed for
the unit tests, which, like the examples, are skipped when Astra2D is a subproject.

## Conventions

- Namespace `astra2d`; implementation details go in `astra2d::detail` and are not part of the API.
- Classes that own OS or GPU resources are non-copyable (RAII).
- Logging goes to stderr, so stdout stays free for program output such as benchmark CSV.
- `ASTRA_ASSERT` checks programmer errors in debug builds; recoverable errors (missing files, failed
  context creation) throw exceptions.
- Formatting follows `.clang-format` (4 spaces, 100 columns, function braces on their own line) and
  the code passes the `.clang-tidy` checks (bugprone, cppcoreguidelines, modernize, performance,
  readability, the clang static analyzer, and more). Both are enforced in CI.

<div class="astra-pager">
<span class="astra-pager-prev">← @ref getting-started "Getting started"</span>
<span class="astra-pager-next">Next: @ref core_guide "Core" →</span>
</div>
