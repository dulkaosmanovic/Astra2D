#!/usr/bin/env bash
#
# Commands for the Astra2D Docker image. Also works directly on a Linux host with the same tools.
#
#   test              build with ASan + UBSan and run the unit tests (default)
#   valgrind          build without sanitizers and run the unit tests under Valgrind
#   release           optimized build (as used for benchmarks)
#   docs [DIR]        generate the documentation; fails on Doxygen warnings.
#                     Copies the HTML to DIR (default /out) if that directory exists.
#   sandbox [SECONDS] smoke tests: run the sandbox for a few seconds (default 5), every other
#                     example for 2 seconds and renderer_stress for a few frames, on an Xvfb
#                     virtual display when no display is available
#   lint              check formatting (clang-format) and run static analysis (clang-tidy)
#   all               lint, test, valgrind, release, docs and sandbox
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

# Runs "$@" on the real display, or on an Xvfb virtual display when there is none.
on_display() {
    if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
        xvfb-run -a -s "-screen 0 1280x720x24" "$@"
    else
        "$@"
    fi
}

# Runs an interactive program for $1 seconds. It must still be running when it is stopped (exit
# code 124 from timeout); an earlier exit is an error. Leak checking is off: the process is killed,
# and GL drivers keep allocations alive until exit.
run_for() {
    local seconds=$1
    shift
    local status=0
    ASAN_OPTIONS=detect_leaks=0 on_display timeout "$seconds" "$@" || status=$?
    if [[ $status -ne 124 ]]; then
        echo "$* exited early with status $status" >&2
        exit 1
    fi
    echo "$* ran for ${seconds}s without errors"
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

cmd_lint() {
    section "Formatting (clang-format)"
    local files
    mapfile -t files < <(find include src examples tests -name '*.cpp' -o -name '*.hpp' | sort)
    clang-format --version
    clang-format --dry-run --Werror "${files[@]}"
    echo "All ${#files[@]} files are formatted"

    section "Static analysis (clang-tidy)"
    # A plain build directory, so the compile database has no sanitizer flags. Only the project's
    # own sources are checked, not wraps (subprojects/) or vendored code (external/).
    setup build-lint
    run-clang-tidy -quiet -p build-lint '^(?!.*/(subprojects|external)/).*/(src|examples|tests)/'
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
    setup build -Db_sanitize=address,undefined
    ninja -C build

    section "Sandbox smoke test (${seconds}s)"
    run_for "$seconds" ./build/examples/Sandbox

    section "Example smoke tests"
    local example
    for example in hello_window shapes sprites text input post_processing coin_collector; do
        run_for 2 "./build/examples/$example"
    done

    # renderer_stress in benchmark mode exits by itself after the given frames; both rendering
    # paths (batched and one draw call per quad) are exercised. It must exit with status 0.
    local mode
    for mode in "" --naive; do
        section "renderer_stress smoke test ${mode:---batched}"
        ASAN_OPTIONS=detect_leaks=0 on_display timeout 120 \
            ./build/examples/renderer_stress --sprites 2000 --frames 30 --warmup 5 $mode
    done
}

case ${1:-test} in
    test)     cmd_test ;;
    valgrind) cmd_valgrind ;;
    release)  cmd_release ;;
    lint)     cmd_lint ;;
    docs)     cmd_docs "${2:-}" ;;
    sandbox)  cmd_sandbox "${2:-}" ;;
    all)      cmd_lint; cmd_test; cmd_valgrind; cmd_release; cmd_docs; cmd_sandbox ;;
    shell)    exec bash ;;
    *)
        sed -n '3,15p' "$0" | sed 's/^# \{0,1\}//'
        exit 2
        ;;
esac
