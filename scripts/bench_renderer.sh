#!/usr/bin/env bash
#
# Evaluation E1: "How much does batching help?"
#
# Runs examples/renderer_stress in a release build, batched and naive, for several sprite counts
# and repetitions, and writes the results to benchmarks/results/e1_renderer.csv together with a
# description of the machine (e1_renderer.machine.txt).
#
#   scripts/bench_renderer.sh                         # defaults below
#   RUNS=5 SPRITES="1000 10000" scripts/bench_renderer.sh
#
# Settings (environment variables):
#   BUILD    build directory               (default build-release, configured if missing)
#   RUNS     repetitions of every case     (default 3)
#   SPRITES  sprite counts                 (default "1000 10000 50000 100000")
#   FRAMES   measured frames per run       (default 600)
#   WARMUP   frames ignored before that    (default 120)
#   OUT      output CSV                    (default benchmarks/results/e1_renderer.csv)
#
# The scene is rendered off-screen at a fixed 1280x720, so the window's size does not matter, but
# keep the window visible (a minimized window pauses the main loop) and the machine otherwise idle.
# Compare render_mean_ms between modes; cpu_mean_ms and gpu_mean_ms split it into the CPU's and the
# GPU's share. frame_mean_ms also contains swap and compositor time.

set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build-release}
RUNS=${RUNS:-3}
SPRITES=${SPRITES:-"1000 10000 50000 100000"}
FRAMES=${FRAMES:-600}
WARMUP=${WARMUP:-120}
OUT=${OUT:-benchmarks/results/e1_renderer.csv}

[[ -f "$BUILD/build.ninja" ]] || meson setup "$BUILD" --buildtype=release
ninja -C "$BUILD" examples/renderer_stress

mkdir -p "$(dirname "$OUT")"
machine="${OUT%.csv}.machine.txt"
{
    echo "date: $(date -Iseconds)"
    echo "commit: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "kernel: $(uname -sr)"
    echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "compiler: $(meson introspect "$BUILD" --compilers |
        python3 -c 'import json,sys; print(json.load(sys.stdin)["host"]["cpp"]["full_version"])')"
    echo "buildtype: $(meson introspect "$BUILD" --buildoptions |
        python3 -c 'import json,sys; print(next(o["value"] for o in json.load(sys.stdin) if o["name"] == "buildtype"))')"
    echo "settings: frames=$FRAMES warmup=$WARMUP runs=$RUNS"
} > "$machine"

echo "run,mode,sprites,frames,width,height,render_mean_ms,render_stddev_ms,render_min_ms,render_max_ms,frame_mean_ms,frame_stddev_ms,draw_calls,quads,cpu_mean_ms,cpu_stddev_ms,gpu_mean_ms,gpu_stddev_ms" > "$OUT"
for run in $(seq 1 "$RUNS"); do
    for sprites in $SPRITES; do
        for mode in batched naive; do
            args=(--sprites "$sprites" --frames "$FRAMES" --warmup "$WARMUP")
            [[ $mode == naive ]] && args+=(--naive)
            # stdout is the CSV line; stderr has the log, which names the GPU and driver.
            line=$("$BUILD/examples/renderer_stress" "${args[@]}" 2> "$BUILD/renderer_stress.log")
            grep -q '^gpu:' "$machine" ||
                echo "gpu: $(grep -oP 'OpenGL \K.*' "$BUILD/renderer_stress.log" | head -1)" >> "$machine"
            echo "$run,$line" | tee -a "$OUT"
        done
    done
done

echo "Results: $OUT"
echo "Machine: $machine"
