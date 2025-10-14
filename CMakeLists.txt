cmake_minimum_required(VERSION 3.14)
project(lwir_compress VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(BUILD_TESTS "Build tests" OFF)
option(ENABLE_NEON "Enable NEON optimizations (ARM only)" ON)

# Platform detection
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(IS_ARM TRUE)
else()
    set(IS_ARM FALSE)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -O3)

    # Work around CharLS 3.0 template issue
    add_compile_options(-fpermissive)

    # NEON optimizations for ARM
    if(IS_ARM AND ENABLE_NEON)
        add_compile_options(-march=armv8-a+simd -fopenmp-simd)
        add_definitions(-DENABLE_NEON=1)
    endif()

    # x86 optimizations
    if(NOT IS_ARM)
        add_compile_options(-march=native)
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# CharLS dependency (will be added as submodule)
set(CHARLS_BUILD_TESTS OFF CACHE BOOL "")
set(CHARLS_BUILD_SAMPLES OFF CACHE BOOL "")
add_subdirectory(external/charls EXCLUDE_FROM_ALL)

# yaml-cpp for configuration
find_package(PkgConfig)
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)
include_directories(${YAML_CPP_INCLUDE_DIRS})

# PNG/TIFF support for reading input frames
find_package(PNG REQUIRED)
# For TIFF, we'll use a simple header-only reader or just focus on PNG initially

# Source files
set(LWIR_COMPRESS_SOURCES
    src/residual.cpp
    src/stats.cpp
    src/decision.cpp
    src/encoder.cpp
    src/config.cpp
    src/pipeline.cpp
    src/bitdepth.cpp
)

set(LWIR_COMPRESS_HEADERS
    include/residual.hpp
    include/stats.hpp
    include/decision.hpp
    include/encoder.hpp
    include/config.hpp
    include/pipeline.hpp
    include/frame.hpp
    include/bitdepth.hpp
)

# Library target (for integration into minifalcon)
add_library(lwir_compress STATIC
    ${LWIR_COMPRESS_SOURCES}
    ${LWIR_COMPRESS_HEADERS}
)

target_link_libraries(lwir_compress
    charls
    ${YAML_CPP_LIBRARIES}
    PNG::PNG
)

target_include_directories(lwir_compress PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Executable target (CLI tool)
add_executable(lwir_compress_tool
    src/main.cpp
)

target_link_libraries(lwir_compress_tool
    lwir_compress
)

# Install executable with original name
install(TARGETS lwir_compress_tool DESTINATION bin RENAME lwir_compress)

# Tests (optional)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# Installation
install(FILES example_config.yaml DESTINATION share/lwir_compress)
