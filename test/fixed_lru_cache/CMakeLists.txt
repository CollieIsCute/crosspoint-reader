add_executable(FixedLruCacheTest
  FixedLruCacheTest.cpp
)

target_link_libraries(FixedLruCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FixedLruCacheTest)
