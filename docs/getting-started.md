# Getting started {#getting-started}

[TOC]

## Quick start

On Arch Linux; the rest of this page explains each step and the alternatives.

```bash
git clone --recurse-submodules https://github.com/dulkaosmanovic/Astra2D.git
cd Astra2D
scripts/setup.sh                   # compiler, Meson, Ninja, libraries; configures build/
ninja -C build                     # the engine, the tests and every example
./build/examples/platformer        # A/D run, Space jump, F3 physics view
```

## Requirements

- Linux with a GPU and driver that support OpenGL 3.3
- GCC 13+ or Clang 17+ (C++20 with `<format>`), Meson 1.1+, Ninja
- Optional: Doxygen and Graphviz (documentation), Valgrind

On Arch Linux, `scripts/setup.sh` installs everything, fetches the submodules and configures a
development build. On other distributions, install a compiler, Meson and Ninja; Meson downloads
missing libraries (GLFW, GLM, spdlog, GoogleTest) from the wraps in `subprojects/`.

The documentation theme is a git submodule. In a clone made without `--recurse-submodules`, run
`git submodule update --init`.

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

Add `-Dwerror=true` to any variant to treat warnings as errors, as CI does. `-Dtests=disabled`,
`-Dexamples=disabled` and `-Dbenchmarks=disabled` skip the unit tests, the examples and the
benchmark programs.

## Examples

Each example is one commented `main.cpp` in `examples/`, so together they form a tutorial. Run them
from the repository root, e.g. `./build/examples/shapes`. Start with `hello_window`, the smallest
program (just the application loop), then go through them in this order:

<div class="astra-gallery">
<div class="astra-example"><img src="shapes.png" alt="shapes"/><div class="astra-example-body"><span class="astra-example-name">shapes</span><span class="astra-example-text">Quads, lines, rectangles and circles</span><span class="astra-keys">drag pans · wheel zooms</span></div></div>
<div class="astra-example"><img src="sprites.png" alt="sprites"/><div class="astra-example-body"><span class="astra-example-name">sprites</span><span class="astra-example-text">Textures, sprite sheets and animation</span><span class="astra-keys">A/D walk</span></div></div>
<div class="astra-example"><img src="text.png" alt="text"/><div class="astra-example-body"><span class="astra-example-name">text</span><span class="astra-example-text">Fonts and UTF-8 text, in the world and on screen</span></div></div>
<div class="astra-example"><img src="input.png" alt="input"/><div class="astra-example-body"><span class="astra-example-name">input</span><span class="astra-example-text">Action mapping, gamepads, rebinding</span><span class="astra-keys">R rebinds jump</span></div></div>
<div class="astra-example"><img src="post_processing.png" alt="post_processing"/><div class="astra-example-body"><span class="astra-example-name">post_processing</span><span class="astra-example-text">Render targets and effects</span><span class="astra-keys">1–4 effect · Tab low resolution</span></div></div>
<div class="astra-example"><img src="scene.png" alt="scene"/><div class="astra-example-body"><span class="astra-example-name">scene</span><span class="astra-example-text">Entities, hierarchy, signals, scene files</span><span class="astra-keys">F5 save · F9 load</span></div></div>
<div class="astra-example"><img src="coin_collector.png" alt="coin_collector"/><div class="astra-example-body"><span class="astra-example-name">coin_collector</span><span class="astra-example-text">A small game on the Scene and ECS</span><span class="astra-keys">F3 physics view · F2 statistics</span></div></div>
<div class="astra-example"><img src="physics_playground.png" alt="physics_playground"/><div class="astra-example-body"><span class="astra-example-name">physics_playground</span><span class="astra-example-text">Bodies, stacks, bullets, one-way platforms</span><span class="astra-keys">click spawns · drag pulls · B broad phase · D debug</span></div></div>
<div class="astra-example"><img src="platformer_level2.png" alt="platformer"/><div class="astra-example-body"><span class="astra-example-name">platformer</span><span class="astra-example-text"><b>Astra Run</b>, the complete game (@ref platformer_guide "guide")</span><span class="astra-keys">A/D run · Space jump · F3 physics view</span></div></div>
<div class="astra-example"><img src="sandbox.png" alt="Sandbox"/><div class="astra-example-body"><span class="astra-example-name">Sandbox</span><span class="astra-example-text">Everything at once; edit <code>crt.frag</code> while it runs</span><span class="astra-keys">WASD · Q/E rotate · P CRT effect</span></div></div>
<div class="astra-example"><img src="renderer_stress.jpg" alt="renderer_stress"/><div class="astra-example-body"><span class="astra-example-name">renderer_stress</span><span class="astra-example-text">Thousands of sprites, batched or not (E1)</span><span class="astra-keys">B batching · Up/Down count</span></div></div>
</div>

`meson test -C build` runs the unit tests; `ninja -C build docs` generates this documentation into
`build/docs/html`.

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
  `subprojects/` directory (for example `git submodule add https://github.com/dulkaosmanovic/Astra2D.git subprojects/astra2d`):
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

Continue with @ref core_guide to see which hook runs when, and why there are two update hooks,
with @ref renderer_guide to draw sprites, and with @ref ecs_guide to organize a game as entities
and systems.

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

<div class="astra-pager">
<span class="astra-pager-prev">← @ref mainpage "Home"</span>
<span class="astra-pager-next">Next: @ref architecture "Architecture" →</span>
</div>
