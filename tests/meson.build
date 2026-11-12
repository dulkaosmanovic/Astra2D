gtest_dep = dependency('gtest', main : true, include_type : 'system')

test_sources = files(
  'core/test_actions.cpp',
  'core/test_assert.cpp',
  'core/test_events.cpp',
  'core/test_input.cpp',
  'core/test_paths.cpp',
  'core/test_profiler.cpp',
  'core/test_signal.cpp',
  'core/test_timestep.cpp',
  'renderer/test_camera.cpp',
  'renderer/test_camera_follow.cpp',
  'renderer/test_camera_shake.cpp',
  'renderer/test_font.cpp',
  'renderer/test_particles.cpp',
  'renderer/test_quad_batch.cpp',
  'renderer/test_renderer_api.cpp',
  'renderer/test_sprite.cpp',
  'renderer/test_tile_layer.cpp',
  'assets/test_asset_loading.cpp',
  'assets/test_file_watcher.cpp',
  'assets/test_resource_cache.cpp',
  'ecs/test_ecs.cpp',
  'scene/test_physics_system.cpp',
  'scene/test_scene.cpp',
  'scene/test_serializer.cpp',
  'scene/test_tiled_map.cpp',
  'physics/test_collision.cpp',
  'physics/test_physics.cpp',
  'audio/test_audio.cpp',
  'debug/test_debug.cpp',
  'debug/test_entity_inspector.cpp',
  'debug/test_entity_moving.cpp',
  'debug/test_entity_picking.cpp',
  'debug/test_entity_rows.cpp',
  'debug/test_frame_graph.cpp',
  'debug/test_frame_history.cpp',
  'debug/test_imgui_style.cpp',
  'debug/test_input_gate.cpp',
  'debug/test_profiler_panel.cpp',
  'debug/test_selection_outline.cpp',
  'examples/test_platformer.cpp',
  # The platformer example's game logic (no OpenGL), tested in examples/test_platformer.cpp.
  '../examples/platformer/level.cpp',
  '../examples/platformer/controller.cpp'
)

test_exe = executable(
  'engine_tests',
  test_sources,
  # src/ for internal headers that are tested directly (e.g. renderer/quad_batch.hpp).
  include_directories : include_directories('../src'),
  # Tests that read the example assets (e.g. the vendored font).
  cpp_args : ['-DASTRA2D_ASSETS_DIR="' + meson.project_source_root() / 'examples' / 'assets' + '"'],
  dependencies : [
    engine_dep,
    gtest_dep
  ]
)

test('engine_tests', test_exe)
