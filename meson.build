project(
  'Astra2D',
  'cpp',
  version : '0.1.0',
  default_options : [
    'cpp_std=c++17',
    'warning_level=3',
    'buildtype=debugoptimized'
  ]
)

# ==========================================
# Sanitizers (Debug only)
# ==========================================

if get_option('buildtype').startswith('debug')
  add_project_arguments('-fsanitize=address', language : 'cpp')
  add_project_link_arguments('-fsanitize=address', language : 'cpp')
endif

# ==========================================
# Dependencies
# ==========================================

glfw_dep = dependency('glfw3')
opengl_dep = dependency('opengl')
spdlog_dep = dependency('spdlog')

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
    'PROJECT_NAME' : meson.project_name()
  }
)

doxygen = find_program('doxygen', required : false)
if doxygen.found()
  run_target('docs',
    command : [doxygen, doxygen_conf]
    )
endif
