# Compiles kvstore.proto into protobuf message stubs and gRPC service stubs
# at build time, and bundles them into a static library (kvstore_proto) that
# the server and client link against.
#
# Tradeoff: uses protobuf_generate() (from upstream protobuf's CMake config)
# rather than raw add_custom_command + protoc. It's terser and tracks
# regeneration correctly; the cost is opacity if you've never seen it before.

add_library(kvstore_proto STATIC kvstore.proto)
target_link_libraries(kvstore_proto PUBLIC
  protobuf::libprotobuf
  gRPC::grpc++
)
target_include_directories(kvstore_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

protobuf_generate(
  TARGET kvstore_proto
  LANGUAGE cpp
  IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

protobuf_generate(
  TARGET kvstore_proto
  LANGUAGE grpc
  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
  IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)
