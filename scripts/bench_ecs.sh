#!/usr/bin/env bash
#
# Evaluation E2: "Is a sparse-set ECS faster than OOP game objects?"
#
# Runs benchmarks/ecs_benchmark in a release build for every approach (ecs, ecs_shuffled, oop,
# oop_shuffled), several entity counts and repetitions, and writes benchmarks/results/e2_ecs.csv
# together with a description of the machine (e2_ecs.machine.txt).
#
#   scripts/bench_ecs.sh
#   RUNS=5 ENTITIES="10000 100000" scripts/bench_ecs.sh
#
# Settings (environment variables):
#   BUILD       build directory                 (default build-release, configured if missing)
#   RUNS        repetitions of every case       (default 5)
#   ENTITIES    object counts                   (default "10000 100000 1000000")
#   WORK        entity updates per run          (default 200000000: 20,000 updates of 10k
#               entities, 200 of 1M, so every run lasts long enough to measure)
#   WARMUP      updates ignored before that     (default 20)
#   OUT         output CSV                      (default benchmarks/results/e2_ecs.csv)
#
# Every run is a new process, so one case cannot warm the caches for the next. Keep the machine
# otherwise idle.

set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build-release}
RUNS=${RUNS:-5}
ENTITIES=${ENTITIES:-"10000 100000 1000000"}
WORK=${WORK:-200000000}
WARMUP=${WARMUP:-20}
OUT=${OUT:-benchmarks/results/e2_ecs.csv}

[[ -f "$BUILD/build.ninja" ]] || meson setup "$BUILD" --buildtype=release
ninja -C "$BUILD" benchmarks/ecs_benchmark

mkdir -p "$(dirname "$OUT")"
machine="${OUT%.csv}.machine.txt"
{
    echo "date: $(date -Iseconds)"
    echo "commit: $(git describe --always --dirty 2>/dev/null || echo unknown)"
    echo "kernel: $(uname -sr)"
    echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "compiler: $(meson introspect "$BUILD" --compilers |
        python3 -c 'import json,sys; print(json.load(sys.stdin)["host"]["cpp"]["full_version"])')"
    echo "buildtype: $(meson introspect "$BUILD" --buildoptions |
        python3 -c 'import json,sys; print(next(o["value"] for o in json.load(sys.stdin) if o["name"] == "buildtype"))')"
    echo "settings: work=$WORK warmup=$WARMUP runs=$RUNS"
} > "$machine"

echo "run,approach,entities,iterations,mean_ms,stddev_ms,min_ms,max_ms,ns_per_entity" > "$OUT"
for run in $(seq 1 "$RUNS"); do
    for entities in $ENTITIES; do
        iterations=$((WORK / entities))
        for approach in ecs ecs_shuffled oop oop_shuffled; do
            line=$("$BUILD/benchmarks/ecs_benchmark" --approach "$approach" --entities "$entities" \
                --iterations "$iterations" --warmup "$WARMUP" 2> /dev/null)
            echo "$run,$line" | tee -a "$OUT"
        done
    done
done

echo "Results: $OUT"
echo "Machine: $machine"
