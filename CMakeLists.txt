# Top-level build entry point. Pulls in the vcpkg toolchain (via the
# VCPKG_ROOT environment variable), enforces C++20, wires ASan+UBSan into
# Debug builds, and descends into proto/, src/, tests/.

cmake_minimum_required(VERSION 3.20)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE STRING "vcpkg toolchain file")
endif()

project(kvstore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Sanitizers on Debug only. Applied as global compile/link options so every
# target in this project (but not vcpkg-installed libraries, which are
# pre-built) gets instrumented. Tradeoff: globalness is simpler than wiring
# per-target, at the cost of being all-or-nothing.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

enable_testing()

add_subdirectory(proto)
add_subdirectory(src)
add_subdirectory(tests)
