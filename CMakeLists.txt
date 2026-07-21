cmake_minimum_required(VERSION 3.20)
project(apollonian_core_ingestor VERSION 1.0.0 LANGUAGES CXX)

# Enforce C++20 Standard for std::span, std::jthread, and low-latency primitives.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDEs (clangd, VSCode).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options.
option(BUILD_TESTS "Build GoogleTest suite" ON)
option(ENABLE_NATIVE_OPTIMIZATION "Enable -march=native CPU optimizations" OFF)
option(ENABLE_SANITIZERS "Enable Address and Thread Sanitizers" OFF)

# Include FetchContent for automatic dependency management.
include(FetchContent)

# Dependencies.
# 1. nlohmann_json dependency resolution.
find_package(nlohmann_json 3.10.0 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found locally. Fetching from GitHub...")
    FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(json)
endif()

# 2. GoogleTest dependency resolution for testing.
if(BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "GoogleTest not found locally. Fetching from GitHub...")
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        )
        # Prevent overriding parent project's compiler/linker settings on Windows.
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
endif()

# Compiler Flags & Sanitizers.
add_library(project_warnings INTERFACE)
add_library(project_options INTERFACE)

if(MSVC)
    target_compile_options(project_warnings INTERFACE /W4 /WX)
else()
    target_compile_options(project_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
    )
endif()

if(ENABLE_NATIVE_OPTIMIZATION AND NOT MSVC)
    target_compile_options(project_options INTERFACE -march=native -O3)
endif()

if(ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(project_options INTERFACE -fsanitize=address,undefined -g)
    target_link_options(project_options INTERFACE -fsanitize=address,undefined)
endif()

# Main Target Definitions.
# Core Shared/Static Library.
add_library(core_ingestor
    src/config.cpp
    src/ingestor.cpp
)

target_include_directories(core_ingestor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(core_ingestor 
    PUBLIC 
        nlohmann_json::nlohmann_json
    PRIVATE 
        project_warnings 
        project_options
)

# Executable Binary.
add_executable(apollonian_ingestor
    src/main.cpp
)

target_link_libraries(apollonian_ingestor PRIVATE
    core_ingestor
    project_warnings
    project_options
)

# Test Suite Execution.
if(BUILD_TESTS)
    add_executable(core_tests
        tests/test_ring_buffer.cpp
        tests/test_serializer.cpp
    )

    target_link_libraries(core_tests PRIVATE
        core_ingestor
        GTest::gtest_main
        project_warnings
        project_options
    )

    include(GoogleTest)
    gtest_discover_tests(core_tests)
endif()
