# Wires GoogleTest into ctest via gtest_discover_tests so each TEST() macro
# shows up as an individual ctest entry.

add_executable(kvstore_tests smoke_test.cpp)
target_link_libraries(kvstore_tests PRIVATE
  GTest::gtest
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(kvstore_tests)
