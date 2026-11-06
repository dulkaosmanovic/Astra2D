project(
  'mygame',
  'cpp',
  version : '0.1.0',
  meson_version : '>=1.1.0',
  default_options : [
    'cpp_std=c++20', # required: Astra2D's public headers use C++20
    'warning_level=3',
    'buildtype=debugoptimized'
  ]
)

# Astra2D from subprojects/: either a checkout in subprojects/astra2d/ (e.g. a git submodule) or
# downloaded by Meson from subprojects/astra2d.wrap. The dependency brings the engine library, its
# headers and the libraries the headers use (GLFW, GLM, spdlog, the GLAD OpenGL loader).
astra2d_dep = dependency('astra2d')

executable(
  'mygame',
  'src/main.cpp',
  # Where the assets are while developing. An installed game finds them next to its executable
  # instead (see findAssets() in main.cpp).
  cpp_args : ['-DGAME_SOURCE_ASSETS_DIR="' + meson.current_source_dir() / 'assets' + '"'],
  dependencies : astra2d_dep,
  install : true
)

# `meson install` puts the assets next to the executable: <prefix>/bin/mygame and
# <prefix>/bin/assets/. Copy that directory to ship the game.
install_subdir('assets', install_dir : get_option('bindir'))
