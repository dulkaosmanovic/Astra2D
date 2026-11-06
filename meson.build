project(
  'Astra2D',
  ['cpp', 'c'],
  version : '0.1.0',
  meson_version : '>=1.1.0',
  default_options : [
    'cpp_std=c++20',
    'warning_level=3',
    'buildtype=debugoptimized',
    'b_ndebug=if-release',
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

# Third-party headers are system includes, so their warnings don't show up in (or, with
# -Dwerror=true, break) the engine build, also when they come from a wrap fallback.
glfw_dep = dependency('glfw3', include_type : 'system')
glm_dep = dependency('glm', include_type : 'system')
spdlog_dep = dependency('spdlog', include_type : 'system')
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

if get_option('examples').disable_auto_if(meson.is_subproject()).allowed()
  subdir('examples')
endif
if get_option('tests').disable_auto_if(meson.is_subproject()).allowed()
  subdir('tests')
endif

# ==========================================
# Doxygen
# ==========================================

fs = import('fs')

doxygen_conf = configure_file(
  input : 'docs/Doxyfile.in',
  output : 'Doxyfile',
  configuration : {
    'PROJECT_NAME' : meson.project_name(),
    'VERSION' : meson.project_version(),
    'SOURCE_ROOT' : meson.project_source_root(),
    'BUILD_ROOT' : meson.project_build_root()
  }
)

# The docs target is only defined for the main project, not when Astra2D is a subproject.
doxygen = find_program('doxygen', required : false)
build_docs = doxygen.found() and not meson.is_subproject()
doxygen_theme = 'external/doxygen-awesome-css/doxygen-awesome.css'
if build_docs and not fs.exists(doxygen_theme)
  warning('Docs theme missing (' + doxygen_theme + '); the docs will be unstyled. ' +
          'Run: git submodule update --init')
endif
if build_docs
  run_target('docs',
    command : [doxygen, doxygen_conf]
  )
endif
