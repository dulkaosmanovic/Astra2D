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
| Graphics | OpenGL 3.3 core, GLAD |
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

The engine is in early development. The build system, dependencies, documentation setup and the
application/window layer (OpenGL 3.3 core context) are in place. Progress is tracked in [PLAN.md](PLAN.md).

---

## Repository Structure

```text
Astra2D/
├── include/astra2d/  # Public engine API
├── src/              # Engine implementation
├── examples/         # Sandbox, demos and the example game
├── tests/            # Unit tests (GoogleTest)
├── docs/             # Doxygen configuration and documentation pages
├── scripts/          # Development and setup scripts
├── external/         # Vendored code
├── subprojects/      # Meson wrap files for dependencies
├── assets/           # Textures, shaders, fonts and other resources
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

```bash
./build/examples/Sandbox      # sandbox application
meson test -C build           # unit tests
ninja -C build docs           # generate documentation into build/docs/html
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
docker run --rm astra2d sandbox 10                  # run the sandbox for 10 s on a virtual display (default 5)
docker run --rm astra2d all                         # lint, tests, Valgrind, release build, docs, sandbox smoke test
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
- the unit tests in each of them, plus the sandbox smoke test (`scripts/docker-entrypoint.sh sandbox`)
  on Xvfb with Mesa's software renderer, which exercises the real window, OpenGL context and main loop;
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

### Adding Astra2D to your own project

Astra2D is a Meson project and exports the dependency `astra2d`. Put it in your project's
`subprojects/` directory, for example as a git submodule:

```bash
git submodule add <repository-url> subprojects/astra2d
```

and use it from your `meson.build`:

```meson
project('mygame', 'cpp', default_options : ['cpp_std=c++20'])

astra2d_dep = dependency('astra2d')   # found in subprojects/astra2d
executable('mygame', 'main.cpp', dependencies : astra2d_dep)
```

The dependency brings the engine library, its headers (`#include <astra2d/...>`), GLFW, GLM,
spdlog and the GLAD OpenGL loader. Your project must use C++20, because the public headers do.
When Astra2D is a subproject, its tests, examples and docs target are not built, so GoogleTest is
not needed; enable them with `-Dastra2d:tests=enabled` or `-Dastra2d:examples=enabled`.

Inside this repository, examples use `engine_dep` directly, as `examples/meson.build` does.

More documentation on rendering, entities, physics and scenes will be added as these systems are implemented.

---

## Documentation

Documentation is published at **https://dulkaosmanovic.github.io/Astra2D/** on every push to `main`.
To generate it locally, run `ninja -C build docs` and open `build/docs/html/index.html`.
It contains a getting-started guide, the architecture overview, a guide per module and the API reference.

---

## License

This project is licensed under the MIT License.

---

## Author

**Đulka Osmanović**: diploma thesis (Diplomski rad)

- Thesis title:
- University / faculty:
- Mentor:
