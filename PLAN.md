# Astra2D — Development Plan

Target: finish in about 3 months, without fixed deadlines.
Each module lists what is **required** for the thesis and what is a **stretch** goal.
Required items are the minimum for a defensible thesis; stretch items are added only when the required ones are done.

> Rule of thumb: finish **all required items of all modules** before starting any stretch item.
> A complete simple engine is worth more than an incomplete advanced one.

## Key decisions

| Area | Decision | Rationale |
|---|---|---|
| Language | C++20 | concepts, `std::span`, designated initializers |
| Graphics API | OpenGL 3.3 core, GLAD loader (`external/glad`) | wide hardware support; close to WebGL2/GLES3 |
| Windowing/input | GLFW | |
| Math | GLM | |
| Logging | spdlog | |
| ECS | **custom** sparse-set ECS | own work: architecture chapter |
| Physics | **custom** impulse-based solver | own work: physics chapter |
| Dependencies | Meson wraps (`subprojects/*.wrap`), system packages preferred | reproducible builds |
| Platform | Linux; Docker image for reproducible build + test | |
| Tests | GoogleTest, run under ASan/UBSan and Valgrind | |
| Docs | Doxygen + doxygen-awesome-css | |

## Architecture

```text
            ┌──────────────────────────────────────────────┐
  games  →  │ examples/platformer, examples/* demos         │
            └──────────────────────────────────────────────┘
                              │ public API only (include/astra2d/)
            ┌──────────────────────────────────────────────┐
            │ scene     Scene, systems, level loading         │
            │ debug     debug draw, stats (ImGui: stretch)    │
            ├──────────────────────────────────────────────┤
            │ ecs       Registry, sparse sets, views          │
            │ physics   bodies, shapes, broad/narrow, solver  │
            │ renderer  Shader, Texture, Batch2D, Camera      │
            │ assets    texture/shader cache                  │
            ├──────────────────────────────────────────────┤
            │ core      Application, Window, Input, Time,     │
            │           Log, Assert                            │
            │ math      GLM, Rect, Transform helpers           │
            └──────────────────────────────────────────────┘
```

Rules:
- Lower layers never include higher ones. `ecs`, `physics` and `math` do not depend on OpenGL, so they can be unit-tested without a GPU.
- Public headers live in `include/astra2d/<module>/`, implementation in `src/<module>/`.
- Each module has a test file in `tests/`.

## Recommended order

1. Foundation → 2. Core → 3. Renderer → 4. ECS → 5. Physics → 6. Debug tools → 7. Platformer → 8. Evaluation & docs

Write the matching thesis chapter right after finishing each module, while the details are fresh
(see [Thesis chapters](#thesis-chapters)).

---

## 1. Foundation

Required
- [x] Engine is a library; `Application` / `Window` base classes; no `main()` in the engine
- [x] OpenGL 3.3 core via GLAD; C++20
- [x] Meson wraps; sanitizers via `-Db_sanitize`; tests link `gtest_main`
- [x] Doxygen with doxygen-awesome-css submodule
- [ ] README build/run instructions kept up to date

Stretch
- [ ] `Dockerfile` (Arch, Mesa llvmpipe + Xvfb) for build/test/docs
- [ ] GitHub Actions: build + test, publish Doxygen docs (`.github/workflows/docs.yml` is empty)
- [ ] `.clang-format` / `.clang-tidy`

## 2. Core

Required
- [ ] `Log` wrapper over spdlog, `ASTRA_ASSERT` macros
- [ ] Main loop with **fixed timestep** for physics and variable rendering (accumulator, interpolation optional)
- [ ] `Input`: keyboard and mouse; pressed / held / released per frame
- [ ] Window resize handling (viewport, camera aspect)
- [ ] Tests: input state transitions, timestep accumulator

Stretch
- [ ] Action mapping (`"jump" → Space`), gamepad support
- [ ] Generic event dispatcher

## 3. Renderer

Required
- [ ] RAII wrappers: `Shader` (load from file), `VertexArray`, `Buffer`, `Texture2D` (stb_image in `external/`)
- [ ] `OrthographicCamera` (position, zoom; screen ↔ world conversion)
- [ ] `Renderer2D` **batch renderer**: colored and textured quads, multiple texture slots
- [ ] Sprite sheets and frame-based sprite animation
- [ ] Line / rectangle primitives (needed for physics debug draw)
- [ ] Render stats: draw calls, quad count
- [ ] Simple texture/shader cache (load each file once)
- [ ] Demo `examples/renderer_stress` with a switch between batched and naive rendering (used in evaluation)

Stretch
- [ ] Camera rotation, camera shake, parallax layers
- [ ] Shader hot-reload
- [ ] Render-to-texture / post-processing
- [ ] Text rendering (stb_truetype)

## 4. ECS

Required
- [ ] `Entity` = 32-bit id with index + generation (detects stale handles)
- [ ] Sparse-set storage per component type
- [ ] `Registry`: create/destroy entity; add/get/remove/has component
- [ ] `View<A, B, ...>` iterating the smallest set
- [ ] `Scene` owning a registry and running systems in a fixed order
- [ ] Components: `Transform`, `Sprite`, `Animator`, `RigidBody`, `Collider`
- [ ] Tests: lifetime, generation reuse, views, removal during iteration
- [ ] Benchmark vs. a naive OOP `GameObject` with virtual `update()` (used in evaluation)

Stretch
- [ ] Entity hierarchy (parent/child transforms)
- [ ] Component add/remove signals
- [ ] Scene serialization (JSON)

## 5. Physics & collision

Required
- [ ] Shapes: AABB and circle; all pair tests (AABB–AABB, circle–circle, AABB–circle)
- [ ] Contact manifold: normal, penetration depth
- [ ] Impulse resolution with restitution and friction; positional correction
- [ ] Body types: static, dynamic, kinematic; gravity; mass / material
- [ ] Broad phase: uniform grid **or** sort-and-sweep, plus brute force O(n²) for comparison
- [ ] Triggers (overlap without response) and collision callbacks
- [ ] Tilemap collision and ground detection (needed for the platformer)
- [ ] ECS integration: physics system syncs `RigidBody` ↔ `Transform`
- [ ] Tests for every shape pair and resolution edge cases; deterministic stepping
- [ ] Demo `examples/physics_playground` (spawn bodies with the mouse, stacking)

Stretch
- [ ] Oriented boxes and polygons (SAT), contact points for rotation
- [ ] Iterative solver with warm starting (more stable stacking)
- [ ] One-way platforms, sleeping bodies, continuous collision (fast bullets)

## 6. Debug tools

Required
- [ ] Debug draw of colliders, contacts and velocities (toggle with a key)
- [ ] FPS / frame time and render stats (in the window title or log is enough)

Stretch (in priority order)
- [ ] ImGui overlay: stats graph, entity list, component inspector, debug toggles
- [ ] Scoped CPU profiler (`ASTRA_PROFILE_SCOPE`) shown in the overlay
- [ ] Editor features: select/move entities, save scene. **Not needed for the thesis**

## 7. Showcase platformer

Required
- [ ] Original platformer in `examples/platformer`, using only `include/astra2d/`
- [ ] Player controller (run, jump), animated sprites, camera follow
- [ ] One complete level loaded from a simple text/ASCII map file
- [ ] Collectibles, one hazard or enemy type, win/lose condition
- [ ] Original or CC0-licensed assets only

Stretch
- [ ] Coyote time and jump buffering
- [ ] Tiled JSON level loading (nlohmann_json wrap), more levels
- [ ] Audio (miniaudio): jump, pickup, music
- [ ] HUD with text (needs text rendering)

## 8. Evaluation & documentation

Required
- [ ] Benchmarks below, run in a release build
- [ ] Full test run under ASan/UBSan and Valgrind without errors or leaks
- [ ] Doxygen comments on all public APIs
- [ ] README: build, run, usage example

Stretch
- [ ] Architecture page with diagrams in `docs/`
- [ ] Comparison of the engine API with an existing engine/framework (e.g. Godot, raylib)

---

## Evaluation

Each measurement answers a question that the thesis discusses.

| # | Question | Experiment | Metric |
|---|---|---|---|
| E1 | How much does batching help? | `renderer_stress`: batched vs. one draw call per sprite at 1k / 10k / 50k / 100k sprites | frame time, draw calls |
| E2 | Is sparse-set ECS faster than OOP game objects? | iterate and update 10k / 100k / 1M entities, ECS vs. `GameObject` with virtual `update()` | time per update |
| E3 | How much does the broad phase reduce collision cost? | `physics_playground` headless: brute force vs. grid/sort-and-sweep at 100 / 1k / 5k bodies | time per physics step, number of pair tests |
| E4 | Is the physics stable? | stack of N boxes, measure drift over 10 s; energy with restitution 0 and 1 | position error |
| E5 | Is the engine usable? | the platformer: lines of game code vs. engine code; features used | LOC, list of used APIs |

Method: release build (`--buildtype=release`), same machine (document CPU/GPU/driver), vsync off, warm-up frames ignored,
several runs with mean and standard deviation. Store results as CSV in `benchmarks/results/` and make charts from them.

## Thesis chapters

| Chapter | Content | Source in the project |
|---|---|---|
| 1. Introduction | motivation, goals, scope | this plan |
| 2. Theoretical background | game loop, rendering pipeline, ECS, collision detection, rigid body dynamics | references below |
| 3. Existing solutions | Unity, Godot, raylib, SDL, Box2D, EnTT: what they do and why the engine is built from scratch | |
| 4. Architecture | modules, layers, dependencies, build system, tools | Architecture section, Foundation |
| 5. Implementation | one subsection per module: core, renderer, ECS, physics, debug tools | modules 2–6 |
| 6. Example application | the platformer | module 7 |
| 7. Testing and evaluation | unit tests, sanitizers, benchmarks E1–E5 with charts | module 8, Evaluation |
| 8. Conclusion | results, limitations, future work (stretch goals that were not done) | |

## References

- Jason Gregory, *Game Engine Architecture* (3rd ed.), CRC Press: overall engine architecture, game loop
- Robert Nystrom, *Game Programming Patterns*: game loop, update method, component, data locality
- Christer Ericson, *Real-Time Collision Detection*: shapes, broad phase, narrow phase
- Ian Millington, *Game Physics Engine Development*: rigid body dynamics
- Erin Catto, GDC talks (*Fast and Simple Physics using Sequential Impulses*, *Iterative Dynamics*): impulse solver
- Randy Gaul, *How to Create a Custom 2D Physics Engine* (tutorial series): impulse resolution, broad phase
- Glenn Fiedler, *Fix Your Timestep!*: fixed timestep loop
- Michele Caini (skypjack), *ECS back and forth* (blog series): sparse-set ECS
- Joey de Vries, *LearnOpenGL*: OpenGL 3.3 core rendering
- Khronos, *OpenGL 3.3 Core Profile Specification*

## Build variants

```bash
meson setup build -Db_sanitize=address,undefined      # development (ASan + UBSan)
meson setup build-valgrind -Db_sanitize=none          # run under valgrind
meson setup build-release --buildtype=release         # benchmarks
meson setup build-wraps --wrap-mode=forcefallback     # use only wraps

meson test -C build
ninja -C build docs
```

## If time runs short, cut in this order

1. Docker / CI
2. ImGui overlay (keep debug draw and stats in the title)
3. Tiled loading (keep the text map format)
4. Animation polish, extra levels, enemies beyond one type
5. Benchmark E5 and the comparison chapter details

Never cut: batch renderer, ECS, AABB/circle physics with impulse resolution, broad phase, tests, benchmarks E1–E3.
