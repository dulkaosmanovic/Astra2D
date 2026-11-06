gtest_dep = dependency('gtest', main : true, include_type : 'system')

test_sources = files(
  'test_input.cpp',
  'test_timestep.cpp',
  'test_assert.cpp',
  'test_camera.cpp',
  'test_sprite.cpp',
  'test_quad_batch.cpp',
  'test_resource_cache.cpp',
  'test_asset_loading.cpp',
  'test_renderer_api.cpp',
  'test_events.cpp',
  'test_actions.cpp',
  'test_camera_shake.cpp',
  'test_file_watcher.cpp',
  'test_font.cpp',
  'test_paths.cpp',
  'test_ecs.cpp',
  'test_scene.cpp',
  'test_serializer.cpp'
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
