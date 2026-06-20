# Wires GoogleTest into ctest via gtest_discover_tests so each TEST() macro
# shows up as an individual ctest entry.

add_executable(kvstore_tests smoke_test.cpp)
target_link_libraries(kvstore_tests PRIVATE
  GTest::gtest
  GTest::gtest_main
)

# Unit tests for Database. Recompiles kv_store.cc directly into the test
# binary so the storage layer can be exercised without bringing up the server.
add_executable(database_tests
  test_database.cc
  ${CMAKE_SOURCE_DIR}/src/kv_store.cc
)
target_include_directories(database_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(database_tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  kvstore_proto
)

include(GoogleTest)
gtest_discover_tests(kvstore_tests)
gtest_discover_tests(database_tests)
