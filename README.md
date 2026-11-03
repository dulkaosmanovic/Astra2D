# Astra2D

<p align="center">
  <img src="docs/logo2.png" width="500"/>
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
└── meson.build
```

---

## Building the Project

### Requirements

- Linux with an OpenGL 3.3 capable GPU/driver
- GCC 11+ or Clang 14+, Meson 1.1+, Ninja
- Optional: Doxygen, Valgrind

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

---

## Running

```bash
./build/examples/Sandbox      # sandbox application
meson test -C build           # unit tests
ninja -C build docs           # generate documentation into build/docs/html
```

Running under Valgrind:

```bash
ninja -C build-valgrind
valgrind --leak-check=full ./build-valgrind/tests/engine_tests
```

---

## Using the Engine

A game derives from `astra2d::Application` and overrides its hooks:

```cpp
#include <astra2d/core/application.hpp>

class MyGame : public astra2d::Application {
public:
    MyGame() : Application({ .title = "My Game", .width = 1280, .height = 720 }) {}

protected:
    void onStart() override {}
    void onUpdate(float deltaTime) override {}
    void onRender() override {}
};

int main()
{
    MyGame game;
    game.run();
}
```

More documentation on rendering, entities, physics and scenes will be added as these systems are implemented.

---

## Documentation

API documentation is generated with Doxygen (`ninja -C build docs`).

---

## License

This project is licensed under the MIT License.

---

## Author

**Đulka Osmanović**: diploma thesis (Diplomski rad)

- Thesis title:
- University / faculty:
- Mentor:
