cmake_minimum_required(VERSION 3.31)

project(MyKaleidoscope)
set(CMAKE_CXX_STANDARD 23)

# Tested on Windows using msys64

# For linking to LLVM, see: https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project
# We unset the cached variables here, to look for different LLVM installations if `CMAKE_PREFIX_PATH` changes
unset(LLVM_DIR CACHE)
unset(LLVM_FOUND CACHE)
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

add_executable(MyKaleidoscope main.cpp)

target_include_directories(MyKaleidoscope PRIVATE ${LLVM_INCLUDE_DIRS})

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
target_compile_definitions(MyKaleidoscope PRIVATE ${LLVM_DEFINITIONS_LIST})
