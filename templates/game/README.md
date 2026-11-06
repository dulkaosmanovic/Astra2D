# Game template

A minimal game that uses Astra2D the way a game in its own repository does. Copy this directory
somewhere outside the Astra2D repository to start a new game:

```text
mygame/
├── meson.build              # the game's build: dependency('astra2d') and its assets
├── subprojects/
│   └── astra2d.wrap         # where Meson gets the engine from
├── src/main.cpp             # the game
└── assets/                  # the game's textures, shaders, fonts, levels...
```

## Getting the engine

Pick one:

- **Let Meson download it.** `subprojects/astra2d.wrap` clones the engine into
  `subprojects/astra2d/` on the first `meson setup`. Set `revision` to a tag or commit so every
  build uses the same engine version.
- **A git submodule**, to change the engine and the game together:

  ```bash
  git submodule add https://github.com/dulkaosmanovic/Astra2D.git subprojects/astra2d
  ```

  An existing `subprojects/astra2d/` directory is used instead of the wrap.

Either way, `dependency('astra2d')` gives the engine library and its headers. It also gives GLFW,
GLM, spdlog and GLAD, the libraries those headers use. Astra2D's own tests, examples and docs are
not built.

## Build and run

```bash
meson setup build
ninja -C build
./build/mygame
```

## Assets

The game's assets belong to the game, in its own `assets/` directory. The engine only loads
them. `findAssets()` in `main.cpp` looks in two places:

1. `assets/` next to the executable, where `meson install` puts them. This is how the game is
   shipped.
2. The source tree's `assets/` (path compiled in), so a development build runs straight from
   `build/` and picks up edited files.

## Shipping

```bash
meson setup build-release --buildtype=release --prefix=/
meson install -C build-release --destdir "$PWD/dist"
```

`dist/bin/` then contains `mygame` and `assets/`. Copy that directory to another Linux machine to
run the game. Astra2D is always linked statically. GLFW and spdlog (with fmt) are taken from the
system when installed, so the other machine needs them as well as an OpenGL 3.3 driver.

To bundle every library into the executable, build the dependencies from their wraps:

```bash
meson setup build-release --buildtype=release --prefix=/ \
    --wrap-mode=forcefallback -Ddefault_library=static
```

The executable then depends only on the C and C++ runtime libraries and the OpenGL driver. The
GLFW built this way uses X11, which runs under Wayland through XWayland.
