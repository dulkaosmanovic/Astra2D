# Astra2D

<p align="center">
  <img src="docs/logo2.png" width="500"/>
</p>

<p align="center">
  <a href="https://github.com/dulkaosmanovic/Astra2D/actions/workflows/ci.yml"><img src="https://github.com/dulkaosmanovic/Astra2D/actions/workflows/ci.yml/badge.svg" alt="Build and test"/></a>
  <a href="https://github.com/dulkaosmanovic/Astra2D/actions/workflows/docs.yml"><img src="https://github.com/dulkaosmanovic/Astra2D/actions/workflows/docs.yml/badge.svg" alt="Documentation"/></a>
  <a href="https://dulkaosmanovic.github.io/Astra2D/"><img src="https://img.shields.io/badge/docs-online-2a7fae" alt="Documentation online"/></a>
</p>


Astra2D is a modular 2D game engine written in C++20 using OpenGL 3.3 core.
It is developed as a university diploma project focused on the architecture and core systems of a real-time game engine:
rendering, an entity component system, physics simulation and collision detection.

Astra2D is a separate engine library that games and tools are built on, not a single game.
The **entity component system and the physics engine are implemented from scratch**, without third-party engine libraries.
The project emphasizes clean architecture, modularity, testability and measurable performance.

---

## Technology

| Purpose | Library / tool |
|---|---|
| Language | C++20 |
| Graphics | OpenGL 3.3 core, GLAD, stb_image (texture loading) |
| Window and input | GLFW |
| Math | GLM |
| Logging | spdlog |
| Build system | Meson + Ninja (dependencies via system packages or Meson wraps) |
| Testing | GoogleTest, AddressSanitizer, UndefinedBehaviorSanitizer, Valgrind |
| Documentation | Doxygen + doxygen-awesome-css |

---

## Features

Core features (required scope of the project):
- application framework and main loop with a fixed physics timestep
- keyboard and mouse input
- batched 2D sprite renderer, textures, sprite animation, orthographic camera
- sparse-set entity component system
- impulse-based 2D physics: AABB and circle colliders, broad phase, triggers, tilemap collision
- debug drawing of colliders and contacts, performance statistics
- example platformer game and per-system demo applications

Planned extensions: ImGui debug overlay, Tiled level loading, audio, text rendering.
See [PLAN.md](PLAN.md) for the full list and the current status.

---

## Current State

The engine is in development. Done so far:

- **core**: application and main loop with a fixed timestep, window, keyboard, mouse and gamepad
  input, action mapping (`"jump" -> Space / A`), type-safe event dispatcher, logging and
  assertions;
- **renderer**: batched 2D renderer (coloured and textured quads, sprites, tiled backgrounds, text,
  debug lines, rectangles and circles), orthographic camera with rotation, parallax and trauma-based
  shake, sprite sheets and frame animation, render targets and post-processing effects, RAII
  OpenGL wrappers, render statistics;
- **assets**: texture, shader, effect and font loading with a cache and hot reloading;
- tooling: CI with sanitizers, Valgrind, clang-format and clang-tidy; Docker image; Doxygen site.

Next: the entity component system. Progress is tracked in [PLAN.md](PLAN.md).

---

## Repository Structure

```text
Astra2D/
├── include/astra2d/  # Public engine API
├── src/              # Engine implementation
├── examples/         # Example programs, from hello_window to a small game, and their assets/
├── templates/game/   # Starting point for a game in its own repository
├── tests/            # Unit tests (GoogleTest)
├── docs/             # Doxygen configuration and documentation pages
├── scripts/          # Development and setup scripts
├── external/         # Vendored code
├── subprojects/      # Meson wrap files for dependencies
├── benchmarks/       # Benchmark results (CSV) for the evaluation
├── PLAN.md           # Development plan
├── meson.options     # Build options (tests, examples)
├── .clang-format     # Code formatting rules
├── .clang-tidy       # Static analysis checks
└── meson.build
```

---

## Building the Project

### Requirements

- Linux with an OpenGL 3.3 capable GPU/driver
- GCC 13+ or Clang 17+ (C++20 with `<format>`), Meson 1.1+, Ninja
- Optional: Doxygen and Graphviz (documentation), Valgrind

On Arch Linux, all dependencies can be installed with:

```bash
./scripts/setup.sh
```

On other distributions, install a compiler, Meson and Ninja. Meson downloads missing libraries
(GLFW, GLM, spdlog, GoogleTest) automatically from the wraps in `subprojects/`.

### Clone

```bash
git clone --recurse-submodules <repository-url>
# or, in an existing clone:
git submodule update --init
```

### Build

```bash
meson setup build -Db_sanitize=address,undefined   # debug build with sanitizers
ninja -C build
```

Other build variants:

```bash
meson setup build-release --buildtype=release       # optimized build (benchmarks)
meson setup build-valgrind -Db_sanitize=none        # for Valgrind (cannot be combined with ASan)
meson setup build-wraps --wrap-mode=forcefallback   # use only Meson wraps, ignore system libraries
```

Add `-Dwerror=true` to treat compiler warnings as errors, as CI does. The unit tests and the examples
can be turned off with `-Dtests=disabled` and `-Dexamples=disabled` (see `meson.options`).

---

## Running

`ninja -C build` builds the engine, the tests and every example. Each example is one commented
`main.cpp` in `examples/`; read them in this order:

| Example | Shows |
|---|---|
| `./build/examples/hello_window` | The smallest program: the application loop |
| `./build/examples/shapes` | Quads, lines, rectangles and circles; camera pan (drag) and zoom (wheel) |
| `./build/examples/sprites` | Textures, sprite sheets and frame animation (A/D walk) |
| `./build/examples/text` | Fonts, UTF-8 text in the world and on the screen |
| `./build/examples/input` | Action mapping, gamepads, rebinding at run time, game events |
| `./build/examples/post_processing` | Render targets and effects (1-4 switch, Tab low resolution) |
| `./build/examples/coin_collector` | A small complete game: states, score, lives, shake, events |
| `./build/examples/Sandbox` | Everything at once: parallax, rotation, CRT effect, hot reloading |
| `./build/examples/renderer_stress` | Benchmark E1: thousands of sprites, batched or not (B, Up/Down, V) |

```bash
meson test -C build      # unit tests
ninja -C build docs      # documentation into build/docs/html
```

### Code style and static analysis

Formatting is defined in [`.clang-format`](.clang-format) and static analysis in
[`.clang-tidy`](.clang-tidy) (the disabled checks are listed there with the reason). Both are checked
in CI; run the same check locally with:

```bash
scripts/docker-entrypoint.sh lint     # clang-format --dry-run --Werror + clang-tidy, fails on any finding
clang-format -i src/core/window.cpp   # reformat a file (most editors can do this on save)
```

CI uses the clang version from the Docker image's package snapshot, because clang-format output can
change between versions. `// NOLINT(check): reason` suppresses a single clang-tidy finding; always
give the reason.

### Running under Valgrind

```bash
ninja -C build-valgrind
valgrind --leak-check=full ./build-valgrind/tests/engine_tests --gtest_filter='-*DeathTest*'
```

### Docker

The `Dockerfile` provides a reproducible environment (Arch Linux pinned to a dated package snapshot,
OpenGL through Mesa's software renderer on a virtual display), so no local dependencies or GPU are needed:

```bash
git submodule update --init                         # the docs theme must be present before the image is built
docker build -t astra2d .
docker run --rm astra2d                             # build with ASan + UBSan and run the tests (default)
docker run --rm astra2d valgrind                    # unit tests under Valgrind (build without sanitizers)
docker run --rm astra2d release                     # optimized build
docker run --rm astra2d lint                        # clang-format + clang-tidy
docker run --rm astra2d sandbox 10                  # run the sandbox for 10 s and every example briefly (default 5)
docker run --rm astra2d template                    # build and run templates/game as a separate project
docker run --rm astra2d all                         # all of the above
docker run --rm -v "$PWD/site:/out" astra2d docs    # generate the documentation into ./site
docker run --rm -it astra2d shell                   # interactive shell
```

The project is built once while the image is built, so a broken build already fails `docker build`.
The same commands work without Docker on a Linux host with the same tools, e.g.
`scripts/docker-entrypoint.sh all`; `sandbox` then uses the real display if one is available.

### Continuous integration

GitHub Actions ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs on every push to `main`
and on every pull request:

- GCC and Clang with ASan + UBSan, and a GCC release build, all with `-Dwerror=true`;
- the unit tests in each of them, plus smoke tests of every example (`scripts/docker-entrypoint.sh sandbox`)
  on Xvfb with Mesa's software renderer, which exercise the real window, OpenGL context and main loop;
- `templates/game` built and run as a separate project with the engine as its subproject
  (`scripts/docker-entrypoint.sh template`), the way games use Astra2D;
- the unit tests under Valgrind;
- `clang-format` and `clang-tidy` (`scripts/docker-entrypoint.sh lint`) in an Arch Linux container
  pinned to the same package snapshot as the Dockerfile.

The documentation workflow fails on any Doxygen warning and publishes the docs from `main`.

---

## Using the Engine

A game derives from `astra2d::Application` and overrides its hooks:

```cpp
#include <astra2d/core/application.hpp>

class MyGame : public astra2d::Application {
public:
    MyGame() : Application({ .window = { .title = "My Game", .width = 1280, .height = 720 } }) {}

protected:
    void onStart() override {}
    void onFixedUpdate(float fixedDeltaTime) override {}  // physics, 60 times per second
    void onUpdate(float deltaTime) override {}            // input, animation, once per frame
    void onRender() override {}
};

int main()
{
    MyGame game;
    game.run();
}
```

`ApplicationSettings` also sets the simulation rate (`fixedTimestep`, default 1/60 s) and the
longest frame time passed to the game (`maxFrameTime`, default 0.25 s).

Drawing goes through a camera and the batched `Renderer2D`:

```cpp
#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/renderer/sprite.hpp>

class MyGame : public astra2d::Application {
    astra2d::AssetCache m_assets{"assets"};
    astra2d::SpriteSheet m_sheet{m_assets.texture("textures/sprites.png"), {16, 16},
                                 /*margin*/ {1, 1}, /*spacing*/ {2, 2}};
    astra2d::OrthographicCamera m_camera{360.0F};   // 360 world units visible vertically
    astra2d::Renderer2D m_renderer;

    void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }

    void onRender() override
    {
        m_renderer.begin(m_camera);
        m_renderer.drawQuad({0, 0}, {64, 64}, {0.2F, 0.6F, 1.0F, 1.0F});
        m_renderer.drawSprite({100, 0}, {32, 32}, m_sheet.frame(0));
        m_renderer.end();
    }
};
```

See the [renderer guide](https://dulkaosmanovic.github.io/Astra2D/renderer_guide.html) for the
camera, sprite animation and how batching works.

### Benchmarks

`scripts/bench_renderer.sh` runs evaluation E1 (batched vs. one draw call per sprite at 1k to 100k
sprites) in a release build and writes `benchmarks/results/e1_renderer.csv` plus a description of
the machine. Run it on an otherwise idle machine with the window visible.

### Making a game with Astra2D

A game lives in its own repository and uses the engine as a library. The engine is not copied into
the game. [`templates/game/`](templates/game/) is a ready-made starting point: copy the directory
and build it.

```text
mygame/
├── meson.build              # dependency('astra2d'), the game executable, its assets
├── subprojects/astra2d.wrap # where Meson gets the engine (or a git submodule in subprojects/astra2d)
├── src/main.cpp             # class MyGame : public astra2d::Application
└── assets/                  # the game's own textures, shaders, fonts
```

```meson
project('mygame', 'cpp', default_options : ['cpp_std=c++20'])

astra2d_dep = dependency('astra2d')   # from subprojects/
executable('mygame', 'src/main.cpp', dependencies : astra2d_dep, install : true)
install_subdir('assets', install_dir : get_option('bindir'))
```

The dependency brings the engine library, its headers (`#include <astra2d/...>`), GLFW, GLM,
spdlog and the GLAD OpenGL loader. Your project must use C++20, because the public headers do.
When Astra2D is a subproject, its tests, examples and docs target are not built, so GoogleTest is
not needed. Enable them with `-Dastra2d:tests=enabled` or `-Dastra2d:examples=enabled`.

**Assets belong to the game, not to the engine.** The engine only loads files. Its own shaders are
compiled into the library, so a game needs no files from the engine at run time. The textures and
fonts in `examples/assets/` are there only for the examples. A shipped game finds its `assets/`
next to its executable with `astra2d::executableDirectory()`, and falls back to the source tree
during development:

```cpp
#include <astra2d/core/paths.hpp>

astra2d::AssetCache assets(astra2d::findDirectory(
    {astra2d::executableDirectory() / "assets", GAME_SOURCE_ASSETS_DIR}));
```

`meson install` places the executable and `assets/` side by side, ready to copy to another
machine. The [template README](templates/game/README.md) covers shipping, including a fully static
build. CI builds and runs the template as a separate project on every push, so this setup keeps
working.

Inside this repository, examples use `engine_dep` directly, as `examples/meson.build` does.

More documentation on rendering, entities, physics and scenes will be added as these systems are implemented.

---

## Documentation

Documentation is published at **https://dulkaosmanovic.github.io/Astra2D/** on every push to `main`.
To generate it locally, run `ninja -C build docs` and open `build/docs/html/index.html`.
It contains a getting-started guide, the architecture overview, a guide per module and the API reference.

---

## License

This project is licensed under the MIT License. Vendored third-party code keeps its own license:
GLAD (MIT/Apache-2.0 generated code), stb_image and stb_truetype (public domain / MIT). The example
font in `examples/assets/fonts/` is *Press Start 2P* by CodeMan38, licensed under the SIL Open Font
License 1.1 (`examples/assets/fonts/OFL.txt`); the other example assets are generated by
`scripts/generate_assets.py`.

---

## Author

**Đulka Osmanović**: diploma thesis (Diplomski rad)

- Thesis title:
- University / faculty:
- Mentor:
