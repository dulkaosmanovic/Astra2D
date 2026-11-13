#!/usr/bin/env bash
#
# The whole evaluation: E1-E5 in a release build, then the charts and the summary tables.
#
#   scripts/bench_all.sh
#
# Runs scripts/bench_renderer.sh (E1, opens a window: keep it visible), bench_ecs.sh (E2),
# bench_physics.sh (E3, E4) and bench_usability.py (E5), then scripts/make_charts.py, which writes
# docs/images/evaluation/*.svg and benchmarks/results/summary.md. The settings of each script
# (RUNS, FRAMES, ...) can be passed as environment variables. Keep the machine otherwise idle: the
# whole run takes about half an hour.

set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build-release}
[[ -f "$BUILD/build.ninja" ]] || meson setup "$BUILD" --buildtype=release
# Build everything first, so no compiler runs while something is measured.
ninja -C "$BUILD"

scripts/bench_renderer.sh
scripts/bench_ecs.sh
scripts/bench_physics.sh
python3 scripts/bench_usability.py
python3 scripts/make_charts.py
