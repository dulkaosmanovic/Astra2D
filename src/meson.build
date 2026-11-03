engine_sources = files(
  'core/application.cpp',
  'core/assert.cpp',
  'core/input.cpp',
  'core/log.cpp',
  'core/timestep.cpp',
  'core/window.cpp'
)

engine_deps = [
  glfw_dep,
  glad_dep,
  glm_dep,
  spdlog_dep,
  opengl_dep
]

engine_lib = library(
  'astra2d',
  engine_sources,
  include_directories : engine_inc,
  dependencies : engine_deps
)

engine_dep = declare_dependency(
  include_directories : engine_inc,
  link_with : engine_lib,
  dependencies : engine_deps
)
