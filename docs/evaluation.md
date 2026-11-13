# Evaluation {#evaluation}

[TOC]

Each experiment answers one question about the engine's design. All of them run from scripts, so
they can be repeated; the numbers on this page come from one run of `scripts/bench_all.sh`.

<div class="astra-summary"><b>In short</b>
<ul>
<li>**E1:** batching cuts 100,000 draw calls to 10 and makes the render 2.8× faster.</li>
<li>**E2:** at 1M objects the ECS updates 3.4× faster than OOP objects, as long as its pools are in the same order; shuffled, it is slower than shuffled objects.</li>
<li>**E3:** at 5,000 bodies the uniform grid makes the physics step 67× faster than brute force.</li>
<li>**E4:** aligned towers of 20 boxes stand without drift; with the default 8 solver iterations, towers stacked with small offsets stand up to about 10 boxes; an elastic ball bounces back to its drop height within 0.06 %.</li>
<li>**E5:** the complete platformer is 18 % of the engine's size.</li>
</ul></div>

Setup of the runs:

- **Machine:** AMD Ryzen 5 3500U (4 cores, 8 threads), Radeon Vega 8 (integrated), 2019 laptop
- **System:** Arch Linux, kernel 7.2.11, Mesa 26.2.5 (radeonsi), OpenGL 4.6 core profile
- **Build:** GCC 16.2.1, `--buildtype=release` (`-O3`)
- **Method:** release build, vsync off, warm-up ignored, 5 runs per case (10 for E1) as separate processes, mean ± sample standard deviation

The raw results are CSV files in `benchmarks/results/` with a `*.machine.txt` next to each;
`benchmarks/results/summary.md` has every case, and `benchmarks/README.md` describes the programs
and the columns. These runs were made in a normal desktop session; the final numbers for a report
should come from a run on an otherwise idle machine (`scripts/bench_all.sh`, about half an hour).


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

All five experiments come from one run of `scripts/bench_all.sh` (commit `1115544` with the review
fixes on top), E1 with 10 runs per case.

![Render time per frame, batched and naive (log scales)](e1_renderer.svg)

| sprites | batched (ms) | naive (ms) | naive / batched | draw calls |
|---:|---:|---:|---:|---:|
| 1,000 | 1.35 ± 0.05 | 2.32 ± 0.09 | 1.7× | 1 / 1,000 |
| 10,000 | 4.70 ± 0.19 | 9.52 ± 0.04 | 2.0× | 1 / 10,000 |
| 50,000 | 16.3 ± 0.2 | 41.8 ± 0.2 | 2.6× | 5 / 50,000 |
| 100,000 | 29.2 ± 0.7 | 82.3 ± 0.4 | 2.8× | 10 / 100,000 |

**Batching makes rendering 2 to 3 times faster, but the gain is smaller than the draw call counts
suggest.** Batching removes 99.99 % of the draw calls, yet the time falls only by a factor of 1.7
to 2.8. The CPU
and GPU times show why:

![Where the batched render time goes (log scales)](e1_split.svg)

| sprites | mode | render (ms) | CPU (ms) | GPU (ms) |
|---:|---|---:|---:|---:|
| 10,000 | batched | 4.70 | 2.01 | 1.38 |
| 10,000 | naive | 9.52 | 8.38 | 2.18 |
| 100,000 | batched | 29.2 | 14.3 | 13.5 |
| 100,000 | naive | 82.3 | 81.6 | 21.5 |

- **The naive path is limited by the CPU.** Its CPU time is almost the whole render time, and the
  GPU works in parallel with it. One draw call with its uniforms costs about 0.67 µs of CPU time more
  than one batched sprite ((81.6 − 14.3) ms / 100,000). That is cheap: a modern Mesa driver submits
  small draw calls quickly, and an integrated GPU shares memory with the CPU.
- **The batched path is limited by the work per sprite, split evenly between CPU and GPU.** At
  100,000 sprites the CPU needs 14.3 ms (about 140 ns per sprite to compute four rotated vertices
  and upload them) and the GPU 13.5 ms to draw and blend 25.6 million pixels (100,000 sprites of
  16×16, about 28 times the target's area). The two add up to 95 % of the render time, so they hardly
  overlap: while the CPU builds a batch, the GPU has nothing to draw.
- The GPU also gains from batching (21.5 against 13.5 ms), because each draw call has a fixed cost
  on the GPU too. With few sprites, a fixed part of the render time (about 1 ms: clearing the
  target and waiting for `glFinish()`) is neither CPU nor GPU drawing time, which is why the gain
  is smallest there.

With batching, the engine keeps 60 frames per second (16.7 ms per frame) up to about 45,000 moving
sprites on this laptop, against about 20,000 without. A discrete GPU with a driver that has a higher cost
per draw call would show a larger factor. The next steps for the renderer would be to cut the CPU
work per sprite (instancing, or vertex buffers that are not rebuilt for static sprites) and to let
the CPU build the next batch while the GPU draws the previous one.

## E2: Is a sparse-set ECS faster than OOP game objects?

`benchmarks/ecs_benchmark` updates N objects (`position += velocity * dt`). Every approach stores
the same data per object. *ecs*: component pools, a view over position and velocity; every entity
got its components in the same order, so both pools are in the same order. *ecs_shuffled*: the
same, but velocity added in random order, as after a long game of adding and removing components.
*oop*: one heap object per entity behind a base pointer with a virtual `update()` (three
subclasses take turns, so the compiler cannot turn it into a direct call), allocated in update
order. *oop_shuffled*: the same objects in random order, as after a long game of creating and
destroying them. Each run does 200 million object updates (20,000 updates of 10,000 objects, 200
of a million), so even the smallest case runs long enough to measure.

![Time per object and update](e2_ecs.svg)

| objects | ECS (ns/object) | ECS shuffled | OOP | OOP shuffled | OOP / ECS | OOP shuffled / ECS shuffled |
|---:|---:|---:|---:|---:|---:|---:|
| 10,000 | 2.69 ± 0.47 | 5.01 ± 0.12 | 2.83 ± 0.73 | 8.31 ± 0.64 | 1.1× | 1.7× |
| 100,000 | 3.06 ± 0.64 | 7.99 ± 0.41 | 13.5 ± 0.1 | 29.7 ± 0.3 | 4.4× | 3.7× |
| 1,000,000 | 4.01 ± 0.04 | 50.8 ± 0.3 | 13.5 ± 0.1 | 35.3 ± 0.1 | 3.4× | 0.7× |

**With its pools in the same order, the ECS is 3.4 to 4.4 times faster once the objects no longer
fit in the cache. With the pools out of order it loses that advantage, and at a million objects it
is slower than shuffled game objects.** In order, the view walks two packed arrays, which the CPU
prefetches. Each OOP object is a separate allocation with data the update does not need, so every
update loads a full cache line per object, and a virtual call. Shuffled, an OOP update is one
random load per object; a shuffled ECS update walks the position pool in order but finds each
velocity through the sparse array, two dependent random loads (the sparse page, then the packed
velocity). While the data fits in the caches (up to 100,000 objects here) that is still cheaper
than the game objects; beyond that it is not.

So the sparse-set ECS is fast in its common case, entities created with the same components, and
slow when the pools have drifted apart. Mature sparse-set ECS libraries handle that case by
sorting one pool into the order of another, or by keeping "groups" of pools in the same order
(EnTT); Astra2D does neither, and it is the first thing to add for large, long-running worlds.

At 10,000 objects everything fits in the CPU's caches (about 1 MB of data) and the two in-order
layouts cost about the same. For a small game the layout does not matter; it matters when there
are many objects.

## E3: How much does the broad phase reduce collision cost?

`benchmarks/physics_benchmark` simulates N circles and boxes moving like a gas in a closed box
(no gravity, elastic walls), so the bodies stay spread out and every step costs about the same. The
three broad phases find exactly the same pairs (the simulation is bit-identical), so only the
cost differs.

![Physics step time by broad phase (log scales)](e3_step.svg)

![Bounding box tests per step (log scales)](e3_pair_tests.svg)

| bodies | brute force (ms) | sort and sweep | uniform grid | brute / sweep | brute / grid |
|---:|---:|---:|---:|---:|---:|
| 100 | 0.062 ± 0.005 | 0.029 ± 0.008 | 0.029 ± 0.007 | 2.1× | 2.1× |
| 1,000 | 4.07 ± 0.02 | 0.309 ± 0.008 | 0.247 ± 0.008 | 13.1× | 16.5× |
| 5,000 | 93.9 ± 0.3 | 2.36 ± 0.01 | 1.41 ± 0.02 | 39.8× | 66.8× |

**The broad phase turns the quadratic cost into a nearly linear one: at 5,000 bodies a step takes
1.4 ms with the grid instead of 94 ms.** Brute force tests every pair, n(n−1)/2: 12.5 million box
tests at 5,000 bodies to find 722 candidate pairs (boxes that overlap; about 80 of them are
actually touching), so its time grows 23-fold for 5 times as many bodies. Sort and sweep tests
160,676 boxes (only neighbours along the x-axis); the grid only the bodies that share a cell,
6,851. The scene has few contacts, so these step times are mostly the broad phase: the narrow
phase and solver together take about 0.4 ms. With 100 bodies all three cost a few hundredths
of a millisecond and the simpler methods are just as good; the grid is the default because it
keeps the step time proportional to the number of bodies.

Without a broad phase, the physics alone would exceed the 16.7 ms of a frame at about 2,100 bodies;
with the grid, 5,000 bodies take under 1.5 ms.

## E4: Is the physics stable?

Deterministic, so every case runs once, at 60 steps per second with sleeping off.

**Aligned towers of unit boxes**, 10 seconds (`max_drift_x`: largest sideways movement of any box;
`top_error_y`: how far the top box ends up below its start; `max_angle`: largest rotation):

| boxes | sideways drift | top sinks by | rotation |
|---:|---:|---:|---:|
| 5 | 0 | 0.006 | 0 |
| 10 | 0 | 0.014 | 0 |
| 20 | 0 | 0.064 | 0 |

A perfectly aligned tower of 20 boxes stands without moving sideways or tilting. It settles by
6.4 cm in total: the solver allows each contact a small overlap (`linearSlop`, 5 mm) so that
resting contacts do not flicker, and 20 contacts add up. That is the best case, though: the forces
are symmetric, so nothing can start a tilt.

**Towers stacked by hand**: every box moved sideways by a random amount up to 5 cm, three random
towers per case, 30 seconds, by the solver's velocity iterations (towers still standing at the
end):

| boxes | 4 | 8 (default) | 12 | 16 | 20 | 24 |
|---:|---:|---:|---:|---:|---:|---:|
| 10 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 | 3/3 |
| 15 | 0/3 | 2/3 | 3/3 | 3/3 | 3/3 | 3/3 |
| 20 | 0/3 | 0/3 | 0/3 | 0/3 | 3/3 | 3/3 |

**With the default 8 iterations, uneven towers stand up to about 10 boxes; 20 boxes need 20
iterations.** A sequential impulse solver passes the weight down the tower a little per iteration,
so the iterations it needs grow with the height of the stack; with too few, the leftover error
tips an uneven tower over after several seconds.
Turning off the position correction changes nothing, so it is the velocity solver's convergence.
A game with tall stacks raises `PhysicsSettings::velocityIterations`; the cost grows linearly with
the iterations, and only for bodies that touch. Box2D's newer solver (soft step with substepping)
is the known way to get tall stacks without that.

**A ball dropped from 5 units** (its lowest point 4.5 above the ground), heights at the top of each
bounce:

| restitution | first bounce | last bounce | highest | bounces in 10 s |
|---:|---:|---:|---:|---:|
| 0 | none | none | none | 1 |
| 0.5 | 1.140 | 0.008 | 1.140 | 5 |
| 1 | 4.497 | 4.497 | 4.497 | 5 |

With restitution 0 the ball stops at the first impact. With 0.5 the speed halves at each impact,
so each bounce should reach a quarter of the height before: 1.125 for the first, measured 1.140
(1.3 % higher). With 1 the ball comes back to 4.497 of 4.5 (99.94 %) on every bounce, the same at
the last as at the first; over 600 simulated seconds (313 bounces) too. The simulation neither
gains nor loses energy over time. Measuring kinetic plus potential energy after every step would
show a swing of up to 1.7 % instead: the semi-implicit Euler integrator updates the velocity
before the position, which makes that sum oscillate within a step without any energy being gained.

## E5: Is the engine usable?

The platformer (@ref platformer_guide) is a complete game with four planets (levels), enemies, hazards,
sound and a HUD, written only against the public API. `scripts/bench_usability.py` counts its code
against the engine's and lists the engine names and member functions it uses.

![Lines of code per part](e5_loc.svg)

| part | code lines | comment lines |
|---|---:|---:|
| engine (include/ + src/) | 12,686 | 2,721 |
| unit tests | 6,423 | 325 |
| platformer (the game) | 2,308 | 247 |
| other examples | 41 to 570 each | |

**The game is 18 % of the engine's size.** It names 54 engine types and functions from all eight
modules: most from the scene (21: entities, components, the physics system, Tiled maps) and the
renderer (13), and only a few from the lower layers (`Application`, `ActionMap`, `AudioEngine`,
`AssetCache`). It also calls 81 engine member functions on the objects it holds (`registry()`,
`view<...>()`, `drawSprite()`, `setParent()`, ...), most of them again from the scene and the
renderer; a dozen names such as `position()` exist in several modules, and the script cannot tell
from the text which one a call goes to. That is the intended shape: a game works with the
high-level API, does not touch OpenGL or the ECS storage, and reaches the physics world only for
queries (ray casts, ground checks). The smaller examples show the same at a smaller scale: a window
is 41 lines, a sprite animation 113, a complete small game (`coin_collector`) 512. The tests are half
the size of the engine, which is what made the refactorings during development safe.

## Correctness

| Check | Result |
|---|---|
| Unit tests (GoogleTest), AddressSanitizer + UndefinedBehaviorSanitizer | 417 passed, 2 skipped (a locale that is not installed; the assert test, which is skipped when asserts are on) |
| The same tests under Valgrind (memcheck, leak check) | no errors, no definite or indirect leaks (the 8 death tests are left out: they abort on purpose) |
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
| ecs | 95.6 % | 58.1 % | 82.9 % |
| physics | 95.0 % | 74.1 % | 92.3 % |
| audio | 91.7 % | 71.2 % | 94.3 % |
| scene | 90.5 % | 70.0 % | 96.8 % |
| core | 62.6 % | 56.4 % | 76.0 % |
| debug | 59.4 % | 44.6 % | 62.5 % |
| assets | 49.6 % | 38.8 % | 55.6 % |
| renderer | 48.2 % | 48.1 % | 46.5 % |
| **total** | **75.3 %** (5,014 of 6,663) | **62.3 %** | **78.3 %** |

**The modules that do not depend on OpenGL are covered to 90 % of their lines or more**, which is
what the layering was for: the ECS, the physics, the scene and the audio are tested headless. The
rest needs a window and an OpenGL context: `Application` and `Window` are never run by a unit test
(they make up most of the gap in core); of the debug tools, the overlay and the ImGui layer
(`debug_overlay.cpp`, `imgui_layer.cpp`, 423 lines) need a context, while the panels, the frame
statistics and the entity inspector are tested; and of the renderer and the asset cache only the
parts without OpenGL calls are (vertex building, cameras, sprite sheets, fonts, paths, file
watching). That code is exercised by the examples instead, which CI runs on a virtual
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
