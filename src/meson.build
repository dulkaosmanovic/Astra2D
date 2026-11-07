engine_sources = files(
  'core/actions.cpp',
  'core/application.cpp',
  'core/assert.cpp',
  'core/events.cpp',
  'core/input.cpp',
  'core/log.cpp',
  'core/paths.cpp',
  'core/timestep.cpp',
  'core/window.cpp',
  'renderer/buffer.cpp',
  'renderer/camera.cpp',
  'renderer/camera_shake.cpp',
  'renderer/font.cpp',
  'renderer/framebuffer.cpp',
  'renderer/quad_batch.cpp',
  'renderer/renderer2d.cpp',
  'renderer/shader.cpp',
  'renderer/sprite.cpp',
  'renderer/texture.cpp',
  'ecs/registry.cpp',
  'physics/broad_phase.cpp',
  'physics/collision.cpp',
  'physics/geometry.cpp',
  'physics/tilemap.cpp',
  'physics/world.cpp',
  'scene/physics_system.cpp',
  'scene/scene.cpp',
  'scene/serializer.cpp',
  'scene/tiled_map.cpp',
  'assets/asset_cache.cpp',
  'audio/audio_engine.cpp',
  'assets/file_watcher.cpp'
)

engine_deps = [
  glfw_dep,
  glad_dep,
  glm_dep,
  spdlog_dep,
  json_dep,
  opengl_dep
]

engine_lib = library(
  'astra2d',
  engine_sources,
  include_directories : engine_inc,
  # stb_image (Texture2D) and miniaudio (AudioEngine) are implementation details, so they are not
  # passed on to engine users.
  dependencies : engine_deps + [stb_dep, miniaudio_dep]
)

engine_dep = declare_dependency(
  include_directories : engine_inc,
  link_with : engine_lib,
  dependencies : engine_deps
)

# Lets other Meson projects use Astra2D as a subproject: dependency('astra2d').
meson.override_dependency('astra2d', engine_dep)
