#!/usr/bin/env bash
#
# Evaluations E3 and E4 of the physics (benchmarks/physics_benchmark, headless).
#
# E3 "How much does the broad phase reduce collision cost?": brute force, sort and sweep and the
#    uniform grid at several body counts -> benchmarks/results/e3_broadphase.csv
# E4 "Is the physics stable?": drift of aligned box towers over 10 s; towers with random sideways
#    offsets, by height and solver iterations; and the bounce heights of a ball with restitution
#    0, 0.5 and 1 -> benchmarks/results/e4_stack.csv, e4_tower.csv, e4_bounce.csv
#
#   scripts/bench_physics.sh
#   RUNS=3 BODIES="100 1000" scripts/bench_physics.sh
#
# Settings (environment variables):
#   BUILD    build directory                    (default build-release, configured if missing)
#   RUNS     repetitions of every E3 case       (default 5)
#   BODIES   body counts for E3                 (default "100 1000 5000")
#   STEPS    measured steps per E3 run          (default 300)
#   WARMUP   steps ignored before that          (default 60)
#   BOXES    tower heights for E4               (default "5 10 20")
#   SIM_SECONDS  simulated time for E4          (default 10)
#   OFFSET   largest sideways offset of a box in the E4 tower sweep, units (default 0.05)
#   SEEDS    random towers per case             (default "1 2 3")
#   TOWERS   tower heights for the sweep        (default "10 15 20")
#   SOLVER_ITERATIONS  velocity iterations for the sweep (default "4 8 12 16 20 24")
#   TOWER_SECONDS  simulated time for the sweep (default 30: some towers fall only after 15 s)
#   OUT      output directory                   (default benchmarks/results)
#
# E4 is deterministic (same result every run), so it runs once. Keep the machine otherwise idle
# for E3.

set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build-release}
RUNS=${RUNS:-5}
BODIES=${BODIES:-"100 1000 5000"}
STEPS=${STEPS:-300}
WARMUP=${WARMUP:-60}
BOXES=${BOXES:-"5 10 20"}
SIM_SECONDS=${SIM_SECONDS:-10}
OFFSET=${OFFSET:-0.05}
SEEDS=${SEEDS:-"1 2 3"}
TOWERS=${TOWERS:-"10 15 20"}
SOLVER_ITERATIONS=${SOLVER_ITERATIONS:-"4 8 12 16 20 24"}
TOWER_SECONDS=${TOWER_SECONDS:-30}
OUT=${OUT:-benchmarks/results}

[[ -f "$BUILD/build.ninja" ]] || meson setup "$BUILD" --buildtype=release
ninja -C "$BUILD" benchmarks/physics_benchmark
bench="$BUILD/benchmarks/physics_benchmark"

mkdir -p "$OUT"
{
    echo "date: $(date -Iseconds)"
    echo "commit: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "kernel: $(uname -sr)"
    echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "compiler: $(meson introspect "$BUILD" --compilers |
        python3 -c 'import json,sys; print(json.load(sys.stdin)["host"]["cpp"]["full_version"])')"
    echo "buildtype: $(meson introspect "$BUILD" --buildoptions |
        python3 -c 'import json,sys; print(next(o["value"] for o in json.load(sys.stdin) if o["name"] == "buildtype"))')"
    echo "settings: steps=$STEPS warmup=$WARMUP runs=$RUNS seconds=$SIM_SECONDS"
} > "$OUT/e3_e4_physics.machine.txt"

e3="$OUT/e3_broadphase.csv"
echo "run,$("$bench" broadphase --bodies 1 --steps 1 --warmup 0 --header 2> /dev/null | head -1)" > "$e3"
for run in $(seq 1 "$RUNS"); do
    for bodies in $BODIES; do
        for method in brute sap grid; do
            line=$("$bench" broadphase --method "$method" --bodies "$bodies" --steps "$STEPS" \
                --warmup "$WARMUP" 2> /dev/null)
            echo "$run,$line" | tee -a "$e3"
        done
    done
done

stack="$OUT/e4_stack.csv"
"$bench" stack --boxes 1 --seconds 0 --header 2> /dev/null | head -1 > "$stack"
for boxes in $BOXES; do
    "$bench" stack --boxes "$boxes" --seconds "$SIM_SECONDS" 2> /dev/null | tee -a "$stack"
done

tower="$OUT/e4_tower.csv"
"$bench" stack --boxes 1 --seconds 0 --header 2> /dev/null | head -1 > "$tower"
for boxes in $TOWERS; do
    for iterations in $SOLVER_ITERATIONS; do
        for seed in $SEEDS; do
            "$bench" stack --boxes "$boxes" --offset "$OFFSET" --seed "$seed" \
                --iterations "$iterations" --seconds "$TOWER_SECONDS" 2> /dev/null | tee -a "$tower"
        done
    done
done

bounce="$OUT/e4_bounce.csv"
"$bench" bounce --seconds 0 --header 2> /dev/null | head -1 > "$bounce"
for restitution in 0 0.5 1; do
    "$bench" bounce --restitution "$restitution" --seconds "$SIM_SECONDS" 2> /dev/null |
        tee -a "$bounce"
done

echo "Results: $e3 $stack $tower $bounce"
