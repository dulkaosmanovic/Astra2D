engine_sources = files(
  'core/application.cpp'
)

engine_static = static_library(
  '2DGameEngineStatic',
  engine_sources,
  include_directories : engine_inc,
  dependencies : [
    glfw_dep,
    opengl_dep,
    spdlog_dep
  ]
)

engine_shared = shared_library(
  '2DGameEngine',
  engine_sources,
  include_directories : engine_inc,
  dependencies : [
    glfw_dep,
    opengl_dep,
    spdlog_dep
  ]
)

engine_dep = declare_dependency(
  include_directories : engine_inc,
  link_with : engine_static
)
