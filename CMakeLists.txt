cmake_minimum_required(VERSION 3.20)
project(memfs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic)

option(MEMFS_SANITIZE "Build with AddressSanitizer + UBSan" OFF)
if(MEMFS_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# The filesystem tree itself -- no FUSE dependency, builds anywhere.
add_library(memfs_core src/memfs.cpp)
target_include_directories(memfs_core PUBLIC src)

enable_testing()
add_executable(memfs_test tests/memfs_test.cpp)
target_link_libraries(memfs_test PRIVATE memfs_core)
add_test(NAME unit COMMAND memfs_test)

# The actual mountable filesystem -- Linux only (needs libfuse3).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(FUSE3 QUIET IMPORTED_TARGET fuse3)
endif()

if(FUSE3_FOUND)
  add_executable(memfs src/main.cpp)
  target_compile_definitions(memfs PRIVATE _FILE_OFFSET_BITS=64)
  target_link_libraries(memfs PRIVATE memfs_core PkgConfig::FUSE3)
  add_test(NAME smoke
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_test.sh $<TARGET_FILE:memfs>)
else()
  message(STATUS "libfuse3 not found: building core library + unit tests only")
endif()
