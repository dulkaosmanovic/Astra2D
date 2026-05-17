gtest_dep = dependency('gtest')

test_sources = files(
  'test_math.cpp',
  'test_collision.cpp'
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
