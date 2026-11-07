# Benchmarks

Results of the evaluation experiments (see "Evaluation" in [PLAN.md](../PLAN.md)). Every run writes
a CSV file and a `*.machine.txt` file describing the machine, build and settings to `results/`.

| Experiment | Script | Output |
|---|---|---|
| E1: batched vs. naive rendering | `scripts/bench_renderer.sh` | `results/e1_renderer.csv` |
| E2: ECS vs. OOP game objects | `scripts/bench_ecs.sh` | `results/e2_ecs.csv` |
| E3: broad phase cost | `scripts/bench_physics.sh` | `results/e3_broadphase.csv` |
| E4: physics stability | `scripts/bench_physics.sh` | `results/e4_stack.csv`, `results/e4_bounce.csv` |
| E5: usability | `scripts/bench_usability.py` | `results/e5_loc.csv`, `results/e5_api.csv` |
| all of the above, then charts | `scripts/bench_all.sh` | also `results/summary.md`, `docs/images/evaluation/*.svg` |

Method: release build, vsync off, warm-up frames ignored, several runs per case. The scene is
rendered into an off-screen framebuffer of a fixed 1280x720 pixels, so the results do not depend on
the size a (tiling) window manager gives the window. Keep the window visible and the machine
otherwise idle while a benchmark runs.

CSV columns for E1:

| Column | Meaning |
|---|---|
| `run`, `mode`, `sprites`, `frames` | repetition, `batched` or `naive`, sprite count, measured frames |
| `width`, `height` | size of the render target in pixels |
| `render_*_ms` | `begin()` to `end()` plus `glFinish()`: the renderer's CPU work and the GPU drawing it causes. **Compare this between modes.** |
| `frame_*_ms` | time between frames: also sprite update, copy to the window, buffer swap and compositor |
| `draw_calls`, `quads` | per frame |

## E2: ECS vs. OOP game objects

`benchmarks/ecs_benchmark.cpp` updates N moving objects (`position += velocity * dt`) and times each
update. Every object has the same data in every approach: position, velocity, and appearance and
health data that the update does not touch, as a real object would carry.

| Approach | Layout |
|---|---|
| `ecs` | Four component pools; the update is a `View<Position, Velocity>` over two packed arrays |
| `oop` | One heap-allocated `GameObject` per object behind a base pointer, virtual `update()`; allocated in update order (best case for this layout) |
| `oop_shuffled` | The same objects updated in random order, as after a long game of creating and destroying objects |

Every run is a separate process with warm-up updates first. CSV columns:

| Column | Meaning |
|---|---|
| `run`, `approach`, `entities`, `iterations` | repetition, layout, object count, measured updates |
| `mean_ms`, `stddev_ms`, `min_ms`, `max_ms` | time of one update of all objects |
| `ns_per_entity` | `mean_ms` per object, in nanoseconds: **compare this** across counts and approaches |

## E3 and E4: physics

`benchmarks/physics_benchmark.cpp` runs the physics without a window.

**E3** (`broadphase --method brute|sap|grid --bodies N`): N circles and boxes (0.5 to 1 units) move
without gravity in a closed box with elastic walls, like a gas, so they stay spread out and every
step costs about the same. All three methods find exactly the same pairs (the simulation is
bit-identical), so only the cost differs. Warm-up steps are ignored. CSV columns (means per step):

| Column | Meaning |
|---|---|
| `run`, `method`, `bodies`, `steps` | repetition, broad phase, body count, measured steps |
| `step_ms`, `stddev_ms` | time of a whole physics step: **compare this** |
| `broadphase_ms`, `narrowphase_ms`, `solver_ms` | the parts of the step |
| `pair_tests` | bounding box tests done by the broad phase: the work it saves |
| `candidate_pairs`, `touching` | pairs passed to the narrow phase, pairs actually touching |

**E4** is deterministic, so every case runs once (10 simulated seconds, sleeping off):

- `stack --boxes N`: a tower of N unit boxes. `max_drift_x` is the largest sideways offset of any
  box, `top_error_y` how far the top box ends up below where it was placed (the allowed overlap of
  all contacts added up), `max_angle` the largest rotation.
- `bounce --restitution e`: a ball dropped from 5 units. `energy_start`, `energy_end`,
  `energy_max` are kinetic plus potential energy per unit mass; with e = 1 it should stay
  constant, with e = 0 the ball should stop at the first impact. `bounces` counts contact begins.

## E5: usability

`scripts/bench_usability.py` counts the lines of the engine (`include/` + `src/`), the tests and
every example: code lines are non-blank lines that are not only a comment, comment lines are counted
apart (`e5_loc.csv`). It also lists every engine name (`astra2d::Name`) the platformer uses, with the
module and header that declare it (`e5_api.csv`), which shows how much of the engine a real game
touches.

## Charts and summary

`scripts/make_charts.py` reads the CSV files and writes one SVG chart per question to
`docs/images/evaluation/` and `results/summary.md` with the mean ± sample standard deviation over
the runs of every case and the ratios the evaluation discusses (naive / batched, OOP / ECS,
brute force / grid). It needs only the Python standard library. The documentation's *Evaluation*
page shows the charts and explains the results.
