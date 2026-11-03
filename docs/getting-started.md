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
  valgrind --leak-check=full ./build-valgrind/tests/engine_tests
  ```
- <b class="tab-title">Wraps only</b>
  Ignore system libraries and build every dependency from `subprojects/`.
  ```bash
  meson setup build-wraps --wrap-mode=forcefallback
  ninja -C build-wraps
  ```

</div>

## Run

| Command | What it does |
|---|---|
| `./build/examples/Sandbox` | Opens the sandbox: arrow keys or WASD move the triangle, Space resets, Escape quits |
| `meson test -C build` | Runs the unit tests |
| `ninja -C build docs` | Generates this documentation into `build/docs/html` |

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

To build it with Meson, add an executable that depends on `engine_dep`, as
`examples/meson.build` does for the sandbox:

```meson
executable('hello', 'hello.cpp', dependencies : engine_dep)
```

Continue with @ref core_guide to see which hook runs when, and why there are two update hooks.
