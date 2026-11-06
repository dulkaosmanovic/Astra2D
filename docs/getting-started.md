# Getting started {#getting-started}

[TOC]

## Requirements

- Linux with a GPU and driver that support OpenGL 3.3
- GCC 13+ or Clang 17+ (C++20 with `<format>`), Meson 1.1+, Ninja
- Optional: Doxygen and Graphviz (documentation), Valgrind

On Arch Linux, `scripts/setup.sh` installs everything, fetches the submodules and configures a
development build. On other distributions, install a compiler, Meson and Ninja; Meson downloads
missing libraries (GLFW, GLM, spdlog, GoogleTest) from the wraps in `subprojects/`.

## Clone

The documentation theme is a git submodule, so clone recursively:

```bash
git clone --recurse-submodules <repository-url>
# or, in an existing clone:
git submodule update --init
```

## Build

Each build variant lives in its own directory, so they can exist side by side.

<div class="tabbed">

- <b class="tab-title">Development</b>
  Debug build with AddressSanitizer and UndefinedBehaviorSanitizer. Use this one day to day.
  ```bash
  meson setup build -Db_sanitize=address,undefined
  ninja -C build
  ```
- <b class="tab-title">Release</b>
  Optimized build with assertions disabled. Use this for benchmarks.
  ```bash
  meson setup build-release --buildtype=release
  ninja -C build-release
  ```
- <b class="tab-title">Valgrind</b>
  Valgrind cannot run programs built with AddressSanitizer, so it gets its own build.
  ```bash
  meson setup build-valgrind -Db_sanitize=none
  ninja -C build-valgrind
  # Death tests abort on purpose, which Valgrind would report as errors.
  valgrind --leak-check=full ./build-valgrind/tests/engine_tests --gtest_filter='-*DeathTest*'
  ```
- <b class="tab-title">Wraps only</b>
  Ignore system libraries and build every dependency from `subprojects/`.
  ```bash
  meson setup build-wraps --wrap-mode=forcefallback
  ninja -C build-wraps
  ```
- <b class="tab-title">Docker</b>
  No local dependencies or GPU needed: Arch Linux pinned to a dated package snapshot, OpenGL through
  Mesa's software renderer on a virtual display.
  ```bash
  git submodule update --init      # docs theme, needed inside the image
  docker build -t astra2d .
  docker run --rm astra2d          # build with ASan + UBSan and run the tests
  docker run --rm astra2d all      # lint, tests, Valgrind, release, docs, smoke tests
  ```
  `lint`, `valgrind`, `release`, `sandbox [SECONDS]`, `docs` and `shell` run single steps; see
  `scripts/docker-entrypoint.sh`. The same script also works directly on a Linux host.

</div>

Add `-Dwerror=true` to any variant to treat warnings as errors, as CI does. `-Dtests=disabled` and
`-Dexamples=disabled` skip the unit tests and the examples.

## Run

| Command | What it does |
|---|---|
| `./build/examples/hello_window` | The smallest program: a window and the application loop |
| `./build/examples/shapes` | Quads, lines, rectangles and circles; drag to pan, wheel to zoom |
| `./build/examples/sprites` | Textures, sprite sheets and frame animation; A/D walk |
| `./build/examples/text` | Fonts, UTF-8 text in the world and on the screen |
| `./build/examples/input` | Action mapping, gamepads, rebinding at run time (R), game events |
| `./build/examples/post_processing` | Render targets and effects: 1-4 switch the effect, Tab low resolution |
| `./build/examples/coin_collector` | A small complete game: title, score, lives, timer, shake, events |
| `./build/examples/Sandbox` | Everything at once: WASD / left stick move, Q/E rotate the camera, wheel zooms, click shakes, P toggles the CRT effect, B batching, Space resets. Edit `examples/assets/shaders/crt.frag` while it runs to see hot reloading |
| `./build/examples/renderer_stress` | Thousands of moving sprites: B toggles batching, Up/Down doubles or halves the count, V toggles vsync |
| `meson test -C build` | Runs the unit tests |
| `ninja -C build docs` | Generates this documentation into `build/docs/html` |

## Code style

The code is formatted with clang-format ([`.clang-format`](https://github.com/dulkaosmanovic/Astra2D/blob/main/.clang-format))
and checked with clang-tidy ([`.clang-tidy`](https://github.com/dulkaosmanovic/Astra2D/blob/main/.clang-tidy)).
CI rejects unformatted code and any clang-tidy finding. Before pushing, run:

```bash
scripts/docker-entrypoint.sh lint
```

Reformat a file with `clang-format -i <file>`, or let the editor format on save (VS Code, CLion and
others pick up `.clang-format` automatically).

## Your first application

An application is a class derived from @ref astra2d::Application. Override the hooks you need and
call @ref astra2d::Application::run "run()" from `main()`:

```cpp
#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>

class Hello : public astra2d::Application {
public:
    Hello() : Application({ .window = { .title = "Hello", .width = 800, .height = 600 } }) {}

protected:
    void onStart() override { ASTRA_INFO("Started"); }

    void onUpdate(float /*dt*/) override
    {
        if (input().isPressed(astra2d::Key::Escape)) {
            quit();
        }
    }

    void onResize(int width, int height) override
    {
        ASTRA_INFO("Framebuffer is now {}x{}", width, height);
    }
};

int main()
{
    Hello app;
    app.run();
}
```

## Building your application

<div class="tabbed">

- <b class="tab-title">In your own project</b>
  This is how a game uses Astra2D: in its own repository, with the engine as a library.
  Astra2D exports the Meson dependency `astra2d`. Place the repository in your project's
  `subprojects/` directory (for example `git submodule add <repository-url> subprojects/astra2d`):
  ```meson
  project('hello', 'cpp', default_options : ['cpp_std=c++20'])

  astra2d_dep = dependency('astra2d')
  executable('hello', 'hello.cpp', dependencies : astra2d_dep)
  ```
  The dependency carries the engine library and headers, GLFW, GLM, spdlog and GLAD. The public
  headers need C++20. As a subproject, Astra2D does not build its tests, examples or docs, so
  GoogleTest is not required (`-Dastra2d:tests=enabled` turns the tests back on).
- <b class="tab-title">Inside this repository</b>
  Add an executable that depends on `engine_dep`, as `examples/meson.build` does for the sandbox:
  ```meson
  executable('hello', 'hello.cpp', dependencies : engine_dep)
  ```

</div>

Continue with @ref core_guide to see which hook runs when, and why there are two update hooks, and
with @ref renderer_guide to draw sprites.

## Assets

The engine needs no asset files at run time: its shaders are compiled into the library. The
textures, shaders and fonts in `examples/assets/` belong to the examples. The examples find them
through the `ASTRA2D_ASSETS_DIR` macro, which `examples/meson.build` sets to that directory, so they
run from the build directory. Set the `ASTRA2D_ASSETS_DIR` environment variable to run an example
binary that was moved away from the source tree. The sprite sheet and backgrounds are original art
generated by `scripts/generate_assets.py`.

A game keeps its own `assets/` directory and ships it next to its executable. @ref
astra2d::executableDirectory and @ref astra2d::findDirectory locate it whatever the working
directory is:

```cpp
astra2d::AssetCache assets(astra2d::findDirectory(
    {astra2d::executableDirectory() / "assets",   // installed game
     GAME_SOURCE_ASSETS_DIR}));                    // development build, set in meson.build
```
