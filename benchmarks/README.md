# Benchmarks

Results of the evaluation experiments (see "Evaluation" in [PLAN.md](../PLAN.md)). Every run writes
a CSV file and a `*.machine.txt` file describing the machine, build and settings to `results/`.

| Experiment | Script | Output |
|---|---|---|
| E1: batched vs. naive rendering | `scripts/bench_renderer.sh` | `results/e1_renderer.csv` |

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
