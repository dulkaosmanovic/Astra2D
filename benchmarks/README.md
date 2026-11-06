# Benchmarks

Results of the evaluation experiments (see "Evaluation" in [PLAN.md](../PLAN.md)). Every run writes
a CSV file and a `*.machine.txt` file describing the machine, build and settings to `results/`.

| Experiment | Script | Output |
|---|---|---|
| E1: batched vs. naive rendering | `scripts/bench_renderer.sh` | `results/e1_renderer.csv` |
| E2: ECS vs. OOP game objects | `scripts/bench_ecs.sh` | `results/e2_ecs.csv` |

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
