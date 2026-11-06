# Examples, from the simplest to a complete game. Each is one main.cpp; build them all with
# `ninja -C build` and run them from the repository root, e.g. ./build/examples/hello_window.
#
# Their textures, shaders and fonts are in examples/assets/: they belong to the examples, not to
# the engine. The path is compiled in, so the examples run from the build directory without
# copying files; the ASTRA2D_ASSETS_DIR environment variable overrides it (common/example_assets.hpp).

example_args = ['-DASTRA2D_ASSETS_DIR="' + meson.current_source_dir() / 'assets' + '"']

examples = {
  'hello_window' : 'hello_window/main.cpp',       # the application loop
  'shapes' : 'shapes/main.cpp',                   # Renderer2D primitives, camera pan and zoom
  'sprites' : 'sprites/main.cpp',                 # textures, sprite sheets, animation
  'text' : 'text/main.cpp',                       # fonts, world and screen-space text
  'input' : 'input/main.cpp',                     # action mapping, gamepads, events
  'post_processing' : 'post_processing/main.cpp', # render targets and effects
  'coin_collector' : 'coin_collector/main.cpp',   # a small complete game
  'scene' : 'scene/main.cpp',                     # ECS, Scene, hierarchy, signals, scene files
  'Sandbox' : 'sandbox/main.cpp',                 # everything at once, hot reloading
  'renderer_stress' : 'renderer_stress/main.cpp', # benchmark E1: batching vs. naive
}

foreach name, source : examples
  executable(
    name,
    files(source),
    cpp_args : example_args,
    dependencies : engine_dep
  )
endforeach
