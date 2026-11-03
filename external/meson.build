# GLAD loader for OpenGL 3.3 core, generated with glad2:
#   glad --api gl:core=3.3 --extensions '' --reproducible --out-path external/glad c

glad_inc = include_directories('glad/include', is_system : true)

glad_lib = static_library(
  'glad',
  'glad/src/gl.c',
  include_directories : glad_inc
)

glad_dep = declare_dependency(
  include_directories : glad_inc,
  link_with : glad_lib
)
