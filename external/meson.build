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

# stb_image for loading textures (PNG, JPEG, BMP, TGA) and stb_truetype for baking fonts.
# Third-party code is compiled without the project's warning flags, and its headers are system
# includes. Both from nothings/stb commit d14d46f6b44456eb88beac58cba833a4135d4c89:
#   external/stb/stb_image.h     v2.30
#   external/stb/stb_truetype.h  v1.26

stb_inc = include_directories('stb', is_system : true)

stb_lib = static_library(
  'stb_image',
  ['stb/stb_image.c', 'stb/stb_truetype.c'],
  include_directories : stb_inc,
  override_options : ['warning_level=0', 'werror=false']
)

stb_dep = declare_dependency(
  include_directories : stb_inc,
  link_with : stb_lib
)

# miniaudio for audio playback (AudioEngine): decoding WAV/FLAC/MP3, mixing and the output device
# (ALSA, PulseAudio, PipeWire or JACK, loaded at run time). Single file, public domain or MIT-0:
#   external/miniaudio/miniaudio.h  v0.11.21 (tag 0.11.21, commit 4a5b74bef029b3592c54b6048650ee5f972c1a48)
# The defines must match in every file that includes the header, so they are passed here.

miniaudio_args = ['-DMA_NO_RESOURCE_MANAGER', '-DMA_NO_ENCODING', '-DMA_NO_GENERATION']
miniaudio_inc = include_directories('miniaudio', is_system : true)

miniaudio_lib = static_library(
  'miniaudio',
  'miniaudio/miniaudio.c',
  include_directories : miniaudio_inc,
  override_options : ['warning_level=0', 'werror=false']
)

miniaudio_dep = declare_dependency(
  include_directories : miniaudio_inc,
  compile_args : miniaudio_args,
  link_with : miniaudio_lib,
  dependencies : [
    dependency('threads'),
    meson.get_compiler('c').find_library('m', required : false),
    meson.get_compiler('c').find_library('dl', required : false)
  ]
)

# Dear ImGui for the debug overlay (DebugOverlay, ImGuiLayer), with its GLFW and OpenGL 3 backends
# and the std::string helpers (misc/cpp/imgui_stdlib). MIT license (imgui/LICENSE.txt):
#   external/imgui  v1.93.0 (tag v1.93.0, commit c1aaac65e896a45f81f47a4d68f9959e0f86c859)
# Its headers are part of the engine's public interface, so games can add their own windows. The
# define must match in every file that includes imgui.h, so it is passed with the dependency.

imgui_args = ['-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS']
imgui_inc = include_directories('imgui', 'imgui/backends', 'imgui/misc/cpp', is_system : true)

imgui_lib = static_library(
  'imgui',
  [
    'imgui/imgui.cpp',
    'imgui/imgui_demo.cpp',
    'imgui/imgui_draw.cpp',
    'imgui/imgui_tables.cpp',
    'imgui/imgui_widgets.cpp',
    'imgui/misc/cpp/imgui_stdlib.cpp',
    'imgui/backends/imgui_impl_glfw.cpp',
    'imgui/backends/imgui_impl_opengl3.cpp'
  ],
  cpp_args : imgui_args,
  include_directories : imgui_inc,
  dependencies : [glfw_dep, meson.get_compiler('cpp').find_library('dl', required : false)],
  override_options : ['warning_level=0', 'werror=false']
)

imgui_dep = declare_dependency(
  include_directories : imgui_inc,
  compile_args : imgui_args,
  link_with : imgui_lib
)
