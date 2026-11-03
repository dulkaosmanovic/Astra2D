#!/usr/bin/env bash
#
# Commands for the Astra2D Docker image. Also works directly on a Linux host with the same tools.
#
#   test              build with ASan + UBSan and run the unit tests (default)
#   valgrind          build without sanitizers and run the unit tests under Valgrind
#   release           optimized build (as used for benchmarks)
#   docs [DIR]        generate the documentation; fails on Doxygen warnings.
#                     Copies the HTML to DIR (default /out) if that directory exists.
#   sandbox [SECONDS] run the sandbox example for a few seconds (default 5) as a smoke test,
#                     on an Xvfb virtual display when no display is available
#   all               test, valgrind, release, docs and sandbox
#   shell             start an interactive shell

set -euo pipefail
cd "$(dirname "$0")/.."

section() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

# Configures a build directory once; later calls only rebuild.
setup() {
    local dir=$1
    shift
    [[ -f "$dir/build.ninja" ]] || meson setup "$dir" "$@"
}

cmd_test() {
    section "Build and test (ASan + UBSan)"
    setup build -Db_sanitize=address,undefined
    ninja -C build
    meson test -C build --print-errorlogs
}

cmd_valgrind() {
    section "Tests under Valgrind"
    setup build-valgrind -Db_sanitize=none
    ninja -C build-valgrind
    # Death tests abort on purpose, which Valgrind would report as errors.
    valgrind --leak-check=full --show-leak-kinds=definite,indirect \
        --errors-for-leak-kinds=definite,indirect --error-exitcode=1 --quiet \
        ./build-valgrind/tests/engine_tests --gtest_filter='-*DeathTest*'
}

cmd_release() {
    section "Release build"
    setup build-release --buildtype=release
    ninja -C build-release
}

cmd_docs() {
    local out=${1:-/out}
    section "Documentation"
    if [[ ! -f external/doxygen-awesome-css/doxygen-awesome.css ]]; then
        echo "Docs theme missing. Run 'git submodule update --init' before building the image." >&2
        exit 1
    fi
    setup build -Db_sanitize=address,undefined
    local log
    log=$(mktemp)
    ninja -C build docs 2>&1 | tee "$log"
    if grep -q 'warning:' "$log"; then
        echo "Doxygen reported warnings:" >&2
        grep 'warning:' "$log" >&2
        exit 1
    fi
    if [[ -d "$out" ]]; then
        cp -r build/docs/html/. "$out/"
        echo "Documentation copied to $out"
    fi
}

cmd_sandbox() {
    local seconds=${1:-5}
    section "Sandbox smoke test (${seconds}s)"
    setup build -Db_sanitize=address,undefined
    ninja -C build
    local run=(timeout "$seconds" ./build/examples/Sandbox)
    if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
        run=(xvfb-run -a -s "-screen 0 1280x720x24" "${run[@]}")
    fi
    # The sandbox runs until it is stopped, so exit code 124 (stopped by timeout) means success.
    # Leak checking is off: the process is killed, and GL drivers keep allocations alive until exit.
    local status=0
    ASAN_OPTIONS=detect_leaks=0 "${run[@]}" || status=$?
    if [[ $status -ne 124 ]]; then
        echo "Sandbox exited early with status $status" >&2
        exit 1
    fi
    echo "Sandbox ran for ${seconds}s without errors"
}

case ${1:-test} in
    test)     cmd_test ;;
    valgrind) cmd_valgrind ;;
    release)  cmd_release ;;
    docs)     cmd_docs "${2:-}" ;;
    sandbox)  cmd_sandbox "${2:-}" ;;
    all)      cmd_test; cmd_valgrind; cmd_release; cmd_docs; cmd_sandbox ;;
    shell)    exec bash ;;
    *)
        sed -n '3,14p' "$0" | sed 's/^# \{0,1\}//'
        exit 2
        ;;
esac
