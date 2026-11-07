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
- [x] README build/run instructions kept up to date

Stretch
- [x] `Dockerfile` (Arch, Mesa llvmpipe + Xvfb) for build/test/docs
- [x] GitHub Actions: build + test (GCC/Clang with ASan+UBSan, release, Valgrind, `-Dwerror=true`), sandbox smoke test on Xvfb, publish Doxygen docs to GitHub Pages
- [x] Usable as a Meson subproject: `dependency('astra2d')`, tests/examples skipped as a subproject
- [x] `executableDirectory()` / `findDirectory()` so a game finds its own assets when installed
- [x] Feature examples (`hello_window`, `shapes`, `sprites`, `text`, `input`, `post_processing`, `coin_collector`), all smoke-tested in CI; example assets moved to `examples/assets/`
- [ ] `.clang-format` / `.clang-tidy`

## 2. Core

Required
- [x] `Log` wrapper over spdlog, `ASTRA_ASSERT` macros
- [x] Main loop with **fixed timestep** for physics and variable rendering (accumulator, interpolation optional)
- [x] `Input`: keyboard and mouse; pressed / held / released per frame
- [x] Window resize handling (viewport, camera aspect)
- [x] Tests: input state transitions, timestep accumulator

Stretch
- [x] Action mapping (`"jump" → Space`), gamepad support
- [x] Generic event dispatcher

## 3. Renderer

Required
- [x] RAII wrappers: `Shader` (load from file), `VertexArray`, `Buffer`, `Texture2D` (stb_image in `external/`)
- [x] `OrthographicCamera` (position, zoom; screen ↔ world conversion)
- [x] `Renderer2D` **batch renderer**: colored and textured quads, multiple texture slots
- [x] Sprite sheets and frame-based sprite animation
- [x] Line / rectangle / circle primitives (needed for physics debug draw)
- [x] Render stats: draw calls, quad count
- [x] Simple texture/shader cache (load each file once)
- [x] Demo `examples/renderer_stress` with a switch between batched and naive rendering (used in evaluation); `scripts/bench_renderer.sh` runs E1

Stretch
- [x] Camera rotation, camera shake, parallax layers
- [x] Shader hot-reload (also textures; `AssetCache::reloadChanged()`)
- [x] Render-to-texture / post-processing
- [x] Text rendering (stb_truetype)

## 4. ECS

Required
- [x] `Entity` = 32-bit id with index + generation (detects stale handles)
- [x] Sparse-set storage per component type
- [x] `Registry`: create/destroy entity; add/get/remove/has component
- [x] `View<A, B, ...>` iterating the smallest set
- [x] `Scene` owning a registry and running systems in a fixed order
- [x] Components: `Transform`, `Sprite`, `Animator`, `RigidBody`, `Collider` (`SpriteRenderer`; `RigidBody` and `Collider` are data until module 5)
- [x] Tests: lifetime, generation reuse, views, removal during iteration
- [x] Benchmark vs. a naive OOP `GameObject` with virtual `update()` (used in evaluation): `benchmarks/ecs_benchmark`, `scripts/bench_ecs.sh` runs E2

Stretch
- [x] Entity hierarchy (parent/child transforms)
- [x] Component add/remove signals
- [x] Scene serialization (JSON)

## 5. Physics & collision

Required
- [x] Shapes: AABB and circle; all pair tests (AABB–AABB, circle–circle, AABB–circle)
- [x] Contact manifold: normal, penetration depth
- [x] Impulse resolution with restitution and friction; positional correction (split impulses)
- [x] Body types: static, dynamic, kinematic; gravity; mass / material
- [x] Broad phase: uniform grid **and** sort-and-sweep, plus brute force O(n²) for comparison; `scripts/bench_physics.sh` runs E3
- [x] Triggers (overlap without response) and collision callbacks (world events, `PhysicsSystem` signals)
- [x] Tilemap collision and ground detection (needed for the platformer): merged tiles, internal edges disabled, `isGrounded()`, ray casts
- [x] ECS integration: physics system syncs `RigidBody` ↔ `Transform` (`PhysicsSystem`, `TilemapCollider`)
- [x] Tests for every shape pair and resolution edge cases; deterministic stepping (also across broad phases)
- [x] Demo `examples/physics_playground` (spawn bodies with the mouse, stacking); `coin_collector` uses triggers and collisions

Stretch
- [x] Oriented boxes and polygons (SAT), contact points for rotation
- [x] Iterative solver with warm starting (more stable stacking), plus a 2-point block solver; E4 in `scripts/bench_physics.sh`
- [x] One-way platforms, sleeping bodies (islands), continuous collision (fast bullets, speculative contacts)

## 6. Debug tools

Required
- [x] Debug draw of colliders, contacts and velocities (toggle with a key): `drawPhysicsDebug()`, F3 in the games, D in `physics_playground`
- [x] FPS / frame time and render stats (in the window title or log is enough): `FrameStats`, F2 logs them

Stretch (in priority order)
- [ ] ImGui overlay: stats graph, entity list, component inspector, debug toggles
- [ ] Scoped CPU profiler (`ASTRA_PROFILE_SCOPE`) shown in the overlay
- [ ] Editor features: select/move entities, save scene. **Not needed for the thesis**

## 7. Showcase platformer

Required
- [x] Original platformer in `examples/platformer` (*Astra Run*), using only `include/astra2d/`
- [x] Player controller (run, jump, jump cut), animated sprites, camera follow (look-ahead, pixel-perfect)
- [x] One complete level loaded from a simple text/ASCII map file (`levels/level1.txt`)
- [x] Collectibles (coins), hazards (spikes, lava) and an enemy type (crawler, stomped from above), win (flag after level 3) / lose (hearts) condition, checkpoints
- [x] Original or CC0-licensed assets only (`scripts/generate_platformer_assets.py`: art, sounds, music)

Stretch
- [x] Coyote time and jump buffering (`JumpController`, unit-tested)
- [x] Tiled JSON level loading (`loadTiledMap`, external tilesets, `makeTileGrid`), more levels (3)
- [x] Audio (miniaudio, `AudioEngine`): jump, pickup, stomp, hurt, music
- [x] HUD with text (hearts, coins, time, level banner, title / pause / game over / victory screens)

## 8. Evaluation & documentation

Required
- [x] Benchmarks below, run in a release build: E1–E5 in `benchmarks/results/`, charts by `scripts/make_charts.py`, results and discussion in `docs/evaluation.md` (`scripts/bench_all.sh` repeats everything)
- [x] Full test run under ASan/UBSan and Valgrind without errors or leaks
- [x] Doxygen comments on all public APIs (`WARN_IF_UNDOCUMENTED`, 0 warnings; CI fails on any)
- [x] README: build, run, usage example

Stretch
- [x] Architecture page with diagrams in `docs/` (module layers, one frame through the modules)
- [x] Comparison of the engine API with an existing engine/framework: raylib and Godot (`docs/comparison.md`)

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
