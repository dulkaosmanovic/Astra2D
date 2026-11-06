# Architecture {#architecture}

[TOC]

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
        debug [label="debug"];
    }
    subgraph cluster_systems {
        label="Systems"; fontname="Helvetica,Arial,sans-serif"; fontsize=10; fontcolor="#5a7b91";
        style="rounded,dashed"; color="#9fbfd3";
        ecs      [label="ecs", fillcolor="#1a2f55", fontcolor="white", URL="\ref ecs"];
        physics  [label="physics"];
        renderer [label="renderer", fillcolor="#1a2f55", fontcolor="white", URL="\ref renderer"];
        assets   [label="assets", fillcolor="#1a2f55", fontcolor="white", URL="\ref assets"];
    }
    subgraph cluster_base {
        label="Foundation"; fontname="Helvetica,Arial,sans-serif"; fontsize=10; fontcolor="#5a7b91";
        style="rounded,dashed"; color="#9fbfd3";
        core [label="core", fillcolor="#1a2f55", fontcolor="white", URL="\ref core"];
        math [label="math"];
    }

    game -> scene; game -> debug;
    scene -> ecs; scene -> physics; scene -> renderer; scene -> assets;
    debug -> renderer; debug -> physics;
    assets -> renderer;
    renderer -> core; assets -> core; ecs -> core; physics -> math; renderer -> math;
}
@enddot

| Module | Responsibility | Uses OpenGL | Status |
|---|---|---|---|
| @ref core "core" | Application, main loop, window, input, timestep, logging, assertions | yes | ready |
| math | GLM, rectangles, transform helpers | no | planned |
| @ref assets "assets" | Load each texture and shader file once | yes | ready |
| @ref renderer "renderer" | Shaders, textures, batched quads, camera, sprites, animation, debug lines | yes | ready |
| @ref ecs "ecs" | Entities, sparse-set component storage, views, signals | no | ready |
| @ref physics "physics" | Bodies, shapes, broad and narrow phase, impulse solver, tile maps | no | ready |
| @ref scene "scene" | Scene with systems, components, hierarchy, sprite rendering, scene files | rendering only | ready |
| debug | Debug drawing and statistics | yes | planned |

`ecs`, `physics` and `math` do not depend on OpenGL. They can be unit-tested and benchmarked
headless, which is what the evaluation chapter relies on.

## Source layout

```text
include/astra2d/<module>/   public headers: the engine API
src/<module>/               implementation
tests/                      one GoogleTest file per module
examples/                   example programs, from hello_window to a small game
examples/assets/            textures, shaders and fonts used by the examples
docs/                       Doxygen configuration, these pages and the theme
external/                   vendored code (GLAD, stb_image, doxygen-awesome-css)
benchmarks/results/         benchmark CSV files and machine descriptions
subprojects/                Meson wraps for GLFW, GLM, spdlog, nlohmann/json and GoogleTest
```

## Dependencies

| Library | Used for |
|---|---|
| GLFW | Window, OpenGL context, input events |
| GLAD | Loading OpenGL 3.3 core functions |
| GLM | Vectors and matrices |
| spdlog | Logging (wrapped by @ref astra2d::log) |
| nlohmann/json | Scene files (@ref astra2d::SceneSerializer); header-only |
| stb_image | Decoding PNG/JPEG/BMP/TGA textures (vendored, private to the engine library) |
| stb_truetype | Baking TrueType fonts into atlases (vendored, private to the engine library) |
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
