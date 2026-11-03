project(
  'Astra2D',
  ['cpp', 'c'],
  version : '0.1.0',
  meson_version : '>=1.1.0',
  default_options : [
    'cpp_std=c++20',
    'warning_level=3',
    'buildtype=debugoptimized',
    'default_library=static'
  ]
)

# ==========================================
# Sanitizers
# ==========================================
#
# Sanitizers are controlled with Meson's built-in option instead of
# hardcoded flags, so a separate build can be used for Valgrind
# (ASan and Valgrind cannot be used together):
#
#   meson setup build-asan -Db_sanitize=address,undefined
#   meson setup build-valgrind -Db_sanitize=none

# ==========================================
# Dependencies
# ==========================================
#
# System packages are used when available; otherwise Meson falls back to
# the wraps in subprojects/ (use --wrap-mode=forcefallback to always use them).

glfw_dep = dependency('glfw3')
glm_dep = dependency('glm')
spdlog_dep = dependency('spdlog')
opengl_dep = dependency('opengl')

subdir('external')

# ==========================================
# Include directories
# ==========================================

engine_inc = include_directories('include')

# ==========================================
# Subdirectories
# ==========================================

subdir('src')
subdir('examples')
subdir('tests')

# ==========================================
# Doxygen
# ==========================================

doxygen_conf = configure_file(
  input : 'docs/Doxyfile.in',
  output : 'Doxyfile',
  configuration : {
    'PROJECT_NAME' : meson.project_name(),
    'SOURCE_ROOT' : meson.project_source_root(),
    'BUILD_ROOT' : meson.project_build_root()
  }
)

doxygen = find_program('doxygen', required : false)
if doxygen.found()
  run_target('docs',
    command : [doxygen, doxygen_conf]
  )
endif
