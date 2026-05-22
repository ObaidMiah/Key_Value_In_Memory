# Builds the server and client binaries. Both link against the generated
# proto library plus runtime libraries (grpc++, spdlog, CLI11).

add_executable(kvstore_server server_main.cpp)
target_include_directories(kvstore_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(kvstore_server PRIVATE
  kvstore_proto
  gRPC::grpc++
  gRPC::grpc++_reflection
  spdlog::spdlog
  CLI11::CLI11
)

add_executable(kvstore_client client_main.cpp)
target_include_directories(kvstore_client PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(kvstore_client PRIVATE
  kvstore_proto
  gRPC::grpc++
  spdlog::spdlog
  CLI11::CLI11
)
