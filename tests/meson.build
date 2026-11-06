gtest_dep = dependency('gtest', main : true, include_type : 'system')

test_sources = files(
  'test_input.cpp',
  'test_timestep.cpp',
  'test_assert.cpp'
)

test_exe = executable(
  'engine_tests',
  test_sources,
  dependencies : [
    engine_dep,
    gtest_dep
  ]
)

test('engine_tests', test_exe)
