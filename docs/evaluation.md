# Evaluation {#evaluation}

[TOC]

Each experiment answers one question about the engine's design. All of them run from scripts, so
they can be repeated; the numbers on this page come from one run of `scripts/bench_all.sh`.

<div class="astra-summary"><b>In short</b>
<ul>
<li>**E1:** batching cuts 100,000 draw calls to 10 and halves the render time.</li>
<li>**E2:** at 1M objects the ECS updates 2.6× faster than OOP objects, 5× faster than scattered ones.</li>
<li>**E3:** at 5,000 bodies the uniform grid makes the physics step 68× faster than brute force.</li>
<li>**E4:** box towers stand without drift; an elastic ball keeps its energy within 2 %.</li>
<li>**E5:** the complete platformer is 18 % of the engine's size.</li>
</ul></div>

Setup of the runs:

- **Machine:** AMD Ryzen 5 3500U (4 cores, 8 threads), Radeon Vega 8 (integrated), 2019 laptop
- **System:** Arch Linux, kernel 7.2.11, Mesa 26.2.5 (radeonsi), OpenGL 4.6 core profile
- **Build:** GCC 16.2.1, `--buildtype=release` (`-O3`), commit `5bcaf36`
- **Method:** release build, vsync off, warm-up ignored, 3 to 5 runs per case as separate processes, mean ± sample standard deviation

The raw results are CSV files in `benchmarks/results/` with a `*.machine.txt` next to each;
`benchmarks/results/summary.md` has every case, and `benchmarks/README.md` describes the programs
and the columns. These runs were made in a normal desktop session; the final numbers for a report
should come from a run on an otherwise idle machine (`scripts/bench_all.sh`, about ten minutes).


## E1: How much does batching help?

`examples/renderer_stress` draws N moving textured sprites into a 1280×720 off-screen target,
either batched (one vertex buffer, up to 16 textures, a draw call per 10,000 quads) or naively (one
draw call with its own uniforms per sprite). Three times are measured per frame:

- *render*: from `begin()` to `end()` plus `glFinish()`, the renderer's CPU work and the GPU work
  it causes;
- *CPU*: from `begin()` to `end()` without waiting for the GPU: building the vertices and handing
  them and the draw calls to the driver;
- *GPU*: the GPU's own time for the same commands, from an OpenGL timer query (`GL_TIME_ELAPSED`).

<div class="astra-figures single">
<div class="astra-figure"><img src="renderer_stress.jpg" alt="<code>renderer_stress</code> with 10,000 moving sprites; B switches between batched and naive drawing"/><span><code>renderer_stress</code> with 10,000 moving sprites; B switches between batched and naive drawing</span></div>
</div>

E1 was run again at commit `7c8f862` when the CPU and GPU times were added; the other experiments
are from `5bcaf36` (the engine code they measure did not change).

![Render time per frame, batched and naive (log scales)](e1_renderer.svg)

| sprites | batched (ms) | naive (ms) | naive / batched | draw calls |
|---:|---:|---:|---:|---:|
| 1,000 | 0.67 ± 0.16 | 1.90 ± 0.14 | 2.8× | 1 / 1,000 |
| 10,000 | 3.71 ± 0.13 | 6.87 ± 0.22 | 1.9× | 1 / 10,000 |
| 50,000 | 14.4 ± 1.1 | 29.1 ± 1.5 | 2.0× | 5 / 50,000 |
| 100,000 | 27.9 ± 0.6 | 56.7 ± 2.0 | 2.0× | 10 / 100,000 |

**Batching halves the cost or better, but the gain is smaller than the draw call counts suggest.**
Batching removes 99.99 % of the draw calls, yet the time falls only by a factor of 2 to 3. The CPU
and GPU times show why:

![Where the batched render time goes (log scales)](e1_split.svg)

| sprites | mode | render (ms) | CPU (ms) | GPU (ms) |
|---:|---|---:|---:|---:|
| 10,000 | batched | 3.71 | 1.73 | 1.10 |
| 10,000 | naive | 6.87 | 5.46 | 2.14 |
| 100,000 | batched | 27.9 | 13.8 | 13.1 |
| 100,000 | naive | 56.7 | 56.1 | 21.6 |

- **The naive path is limited by the CPU.** Its CPU time is almost the whole render time, and the
  GPU works in parallel with it. One draw call with its uniforms costs about 0.42 µs of CPU time more
  than one batched sprite ((56.1 − 13.8) ms / 100,000). That is cheap: a modern Mesa driver submits
  small draw calls quickly, and an integrated GPU shares memory with the CPU.
- **The batched path is limited by the work per sprite, split evenly between CPU and GPU.** At
  100,000 sprites the CPU needs 13.8 ms (about 140 ns per sprite to compute four rotated vertices
  and upload them) and the GPU 13.1 ms to draw and blend 25.6 million pixels (100,000 sprites of
  16×16, about 28 times the target's area). The two add up to almost exactly the render time, so they hardly
  overlap: while the CPU builds a batch, the GPU has nothing to draw.
- The GPU also gains from batching (21.6 against 13.1 ms), because each draw call has a fixed cost
  on the GPU too. With few sprites, a fixed part of the render time (about 0.4 ms: clearing the
  target and waiting for `glFinish()`) is neither CPU nor GPU drawing time.

With batching, the engine keeps 60 frames per second (16.7 ms) up to about 50,000 moving sprites
on this laptop, against about 25,000 without. A discrete GPU with a driver that has a higher cost
per draw call would show a larger factor. The next steps for the renderer would be to cut the CPU
work per sprite (instancing, or vertex buffers that are not rebuilt for static sprites) and to let
the CPU build the next batch while the GPU draws the previous one.

## E2: Is a sparse-set ECS faster than OOP game objects?

`benchmarks/ecs_benchmark` updates N objects (`position += velocity * dt`). Every approach stores
the same data per object. *ecs*: component pools, a view over position and velocity. *oop*: one heap
object per entity behind a base pointer with a virtual `update()`, allocated in update order (the
best case for that layout). *oop_shuffled*: the same objects in random order, as after a long game
of creating and destroying them.

![Time per object and update](e2_ecs.svg)

| objects | ECS (ns/object) | OOP | OOP shuffled | OOP / ECS | shuffled / ECS |
|---:|---:|---:|---:|---:|---:|
| 10,000 | 3.6 ± 1.9 | 2.2 ± 1.2 | 4.2 ± 1.8 | 0.6× | 1.2× |
| 100,000 | 2.07 ± 0.27 | 7.07 ± 0.27 | 12.5 ± 1.5 | 3.4× | 6.0× |
| 1,000,000 | 2.60 ± 0.12 | 6.84 ± 0.15 | 12.9 ± 0.2 | 2.6× | 5.0× |

**Once the objects no longer fit in the cache, the ECS is 2.6 to 3.4 times faster, and 5 to 6 times
faster than objects in a realistic (shuffled) order.** The ECS cost per object stays flat at about
2 to 2.6 ns: the view walks two packed arrays, which the CPU prefetches. Each OOP object is a
separate allocation with data the update does not need, so every update loads a full cache line
per object, and a virtual call; in shuffled order the loads are random and cost twice as much again.

At 10,000 objects everything fits in the CPU's caches (about 1 MB of data), a whole update takes
about 0.03 ms, and the differences are within the measurement noise (standard deviations of 50 %).
For a small game the layout does not matter; it matters when there are many objects, which is what
an engine has to be ready for.

## E3: How much does the broad phase reduce collision cost?

`benchmarks/physics_benchmark` simulates N circles and boxes moving like a gas in a closed box
(no gravity, elastic walls), so the bodies stay spread out and every step costs about the same. The
three broad phases find exactly the same pairs (the simulation is bit-identical), so only the
cost differs.

![Physics step time by broad phase (log scales)](e3_step.svg)

![Bounding box tests per step (log scales)](e3_pair_tests.svg)

| bodies | brute force (ms) | sort and sweep | uniform grid | brute / sweep | brute / grid |
|---:|---:|---:|---:|---:|---:|
| 100 | 0.049 ± 0.008 | 0.026 ± 0.014 | 0.032 ± 0.007 | 1.9× | 1.5× |
| 1,000 | 2.72 ± 0.06 | 0.202 ± 0.010 | 0.157 ± 0.008 | 13.5× | 17.3× |
| 5,000 | 62.4 ± 0.3 | 1.57 ± 0.03 | 0.92 ± 0.04 | 39.6× | 67.9× |

**The broad phase turns the quadratic cost into a nearly linear one: at 5,000 bodies a step takes
0.92 ms with the grid instead of 62 ms.** Brute force tests every pair, n(n−1)/2: 12.5 million box
tests at 5,000 bodies to find 722 touching pairs, so its time grows 23-fold for 5 times as many
bodies. Sort and sweep tests 160,676 boxes (only neighbours along the x-axis); the grid only the
bodies in the same and neighbouring cells, 6,851. With 100 bodies all three cost a few hundredths
of a millisecond and the simpler methods are just as good; the grid is the default because it
keeps the step time proportional to the number of bodies.

Without a broad phase, the physics alone would exceed the 16.7 ms of a frame at about 2,500 bodies;
with the grid, 5,000 bodies take less than a millisecond.

## E4: Is the physics stable?

Deterministic, so every case runs once: 10 simulated seconds at 60 steps per second, sleeping off.

**Towers of unit boxes** (`max_drift_x`: largest sideways offset of any box; `top_error_y`: how far
the top box ends up below its start; `max_angle`: largest rotation):

| boxes | sideways drift | top sinks by | rotation |
|---:|---:|---:|---:|
| 5 | 0 | 0.006 | 0 |
| 10 | 0 | 0.014 | 0 |
| 20 | 0 | 0.064 | 0 |

A tower of 20 boxes stands for 10 seconds without moving sideways or tilting. It settles by
6.4 cm in total: the solver allows each contact a small overlap (`linearSlop`, 5 mm) so that
resting contacts do not flicker, and 19 contacts add up. Warm starting and the two-point block
solver are the parts of the solver that keep a stack still (see @ref physics_guide).

**A ball dropped from 5 units** (energy is kinetic plus potential per unit mass):

| restitution | energy at start | at the end | highest | bounces |
|---:|---:|---:|---:|---:|
| 0 | 44.15 | −0.04 | 44.15 | 1 |
| 0.5 | 44.15 | −0.04 | 44.15 | 5 |
| 1 | 44.15 | 43.81 | 44.91 | 5 |

With restitution 0 the ball stops at the first impact; with 0.5 it bounces lower each time and
comes to rest. With 1 (perfectly elastic) it keeps 99.2 % of its energy after 10 seconds, and the
largest excess is 1.7 %: the simulation does not gain or lose energy in the long run, so a
bouncing ball neither dies down nor climbs.

## E5: Is the engine usable?

The platformer (@ref platformer_guide) is a complete game with four planets (levels), enemies, hazards,
sound and a HUD, written only against the public API. `scripts/bench_usability.py` counts its code
against the engine's and lists the engine names it uses.

![Lines of code per part](e5_loc.svg)

| part | code lines | comment lines |
|---|---:|---:|
| engine (include/ + src/) | 11,796 | 2,583 |
| unit tests | 5,949 | 292 |
| platformer (the game) | 2,173 | 226 |
| other examples | 42 to 568 each | |

**The game is 18 % of the engine's size.** It uses 54 engine names from all eight modules: most
from the scene (21: entities, components, the physics system, Tiled maps) and the renderer (13),
and only a few from the lower layers (`Application`, `ActionMap`, `AudioEngine`, `AssetCache`). That
is the intended shape: a game works with the high-level API and does not touch OpenGL, the physics
world or the ECS storage directly. The smaller examples show the same at a smaller scale: a window
is 42 lines, a sprite animation 115, a complete small game (`coin_collector`) 514. The tests are half
the size of the engine, which is what made the refactorings during development safe.

## Correctness

| Check | Result |
|---|---|
| Unit tests (GoogleTest), AddressSanitizer + UndefinedBehaviorSanitizer | 349 passed, 2 skipped (a locale that is not installed; the assert test, which is skipped when asserts are on) |
| The same tests under Valgrind (memcheck, leak check) | no errors, no definite or indirect leaks (the 7 death tests are left out: they abort on purpose) |
| GCC and Clang with `-Dwerror=true` | no warnings |
| clang-format and clang-tidy (bugprone, cppcoreguidelines, modernize, performance, readability, static analyzer) | no findings |
| Doxygen with `WARN_IF_UNDOCUMENTED` | no warnings: every public type and function is documented |
| Examples on a virtual display | CI runs every example for a few seconds and fails on an early exit or a sanitizer error (`scripts/docker-entrypoint.sh sandbox`) |

### Test coverage

`scripts/coverage.py` builds the engine with `-Db_coverage=true`, runs the unit tests once and
reads GCC's counters with `gcov` (results in `benchmarks/results/coverage.csv`). Every compiled
engine file is counted, also the ones no test uses.

| module | lines | branches | functions |
|---|---:|---:|---:|
| ecs | 96.3 % | 57.3 % | 82.8 % |
| debug | 95.8 % | 91.3 % | 93.3 % |
| physics | 93.6 % | 73.0 % | 89.7 % |
| audio | 91.1 % | 69.5 % | 91.7 % |
| scene | 90.3 % | 69.9 % | 96.0 % |
| core | 57.9 % | 58.8 % | 63.6 % |
| assets | 48.5 % | 31.3 % | 61.1 % |
| renderer | 43.1 % | 45.1 % | 40.3 % |
| **total** | **78.3 %** (4,158 of 5,308) | **64.1 %** | **78.5 %** |

**The modules that do not depend on OpenGL are covered to 90 % of their lines or more**, which is
what the layering was for: the ECS, the physics, the scene, the audio and the debug tools are
tested headless. The rest needs a window and an OpenGL context: `Application` and `Window` are
never run by a unit test (they make up most of the gap in core), and of the renderer and the asset
cache only the parts without OpenGL calls are (vertex building, cameras, sprite sheets, fonts,
paths, file watching). That code is exercised by the examples instead, which CI runs on a virtual
display. Branch coverage is lower than line coverage: gcov counts the branches of every template
instantiation (in the ECS, one per component type) and of every assertion, whose failing side only
the death tests take.

All of these run in CI on every push (`.github/workflows/ci.yml`) and in the Docker image
(`docker run --rm astra2d all`).

## Limits of the evaluation

- One machine, a laptop with an integrated GPU; E1 in particular depends on the GPU and its driver,
  and a desktop GPU would change the factors (not the order).
- E2 measures one simple update; a real system touching more components, or random access by
  entity, would show a different factor, though the same direction.
- E3 uses evenly spread bodies of similar size. Very different sizes make a uniform grid worse
  (big bodies cover many cells), which is where sort and sweep is the safer choice.
- E5 counts lines, which says how much code a game needs, not how easy it was to write; the
  comparison with raylib and Godot (@ref comparison) looks at the API from that side.

<div class="astra-pager">
<span class="astra-pager-prev">← @ref platformer_guide "Example game"</span>
<span class="astra-pager-next">Next: @ref comparison "Comparison" →</span>
</div>
