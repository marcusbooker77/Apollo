# Apollo Performance Optimizations CMake Configuration
# Add this to your main CMakeLists.txt or include it

# ============================================
# COMPILER OPTIMIZATION FLAGS
# ============================================

option(APOLLO_ENABLE_NATIVE_OPTIMIZATIONS "Enable machine-specific ISA tuning for local builds" OFF)
option(APOLLO_ENABLE_FAST_MATH "Enable fast-math optimizations that may change numeric behavior" OFF)

# Enable maximum optimization for Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Applying performance optimizations for Release build")
    
    # Aggressive optimization flags
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(
            -O3                          # Maximum optimization
            -flto                        # Link-time optimization
            -funroll-loops              # Unroll loops
            -fomit-frame-pointer        # Omit frame pointer when not needed
        )

        if(APOLLO_ENABLE_NATIVE_OPTIMIZATIONS)
            add_compile_options(
                -march=native            # Optimize for current CPU
                -mtune=native            # Tune for current CPU
            )
        endif()

        if(APOLLO_ENABLE_FAST_MATH)
            add_compile_options(
                -ffast-math              # Fast math (if safe for your use case)
            )
        endif()
        
        # Additional flags for GCC
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options(
                -fgraphite-identity         # Loop optimization
                -floop-nest-optimize        # Nested loop optimization
            )
        endif()
    elseif(MSVC)
        add_compile_options(
            /O2                          # Maximum optimization
            /Oi                          # Intrinsic functions
            /GL                          # Whole program optimization
        )

        if(APOLLO_ENABLE_NATIVE_OPTIMIZATIONS)
            add_compile_options(/arch:AVX2)
        endif()

        add_link_options(/LTCG)          # Link-time code generation
    endif()
endif()

# ============================================
# LINK-TIME OPTIMIZATION (LTO)
# ============================================

# Enable LTO for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
    
    if(IPO_SUPPORTED)
        message(STATUS "Interprocedural optimization (LTO) enabled")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "IPO is not supported: ${IPO_ERROR}")
    endif()
endif()

# ============================================
# UNITY BUILDS (Faster Compilation)
# ============================================

option(APOLLO_UNITY_BUILD "Enable unity builds for faster compilation" ON)

if(APOLLO_UNITY_BUILD)
    message(STATUS "Unity builds enabled")
    set(CMAKE_UNITY_BUILD ON)
    set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
endif()

# ============================================
# PRECOMPILED HEADERS
# ============================================

option(APOLLO_USE_PCH "Use precompiled headers" ON)

if(APOLLO_USE_PCH)
    message(STATUS "Precompiled headers enabled")
    # Will be applied to targets with target_precompile_headers()
endif()

# Function to add PCH to a target
function(apollo_add_pch TARGET_NAME)
    if(APOLLO_USE_PCH)
        target_precompile_headers(${TARGET_NAME} PRIVATE
            # Standard library headers
            <algorithm>
            <atomic>
            <chrono>
            <cstdint>
            <functional>
            <iostream>
            <memory>
            <mutex>
            <string>
            <string_view>
            <thread>
            <vector>
            
            # Boost headers
            <boost/asio.hpp>
            <boost/log/trivial.hpp>
            
            # FFmpeg headers (if used frequently)
            # <libavcodec/avcodec.h>
            # <libavutil/opt.h>
        )
    endif()
endfunction()

# ============================================
# CCACHE SUPPORT
# ============================================

option(APOLLO_USE_CCACHE "Use ccache to speed up compilation" ON)

if(APOLLO_USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "ccache found: ${CCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    else()
        message(STATUS "ccache not found, compilation caching disabled")
    endif()
endif()

# ============================================
# SECURITY HARDENING
# ============================================

option(APOLLO_ENABLE_HARDENING "Enable security hardening flags" ON)

if(APOLLO_ENABLE_HARDENING AND CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Security hardening enabled")
    
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(
            -D_FORTIFY_SOURCE=2          # Buffer overflow detection
            -fstack-protector-strong     # Stack protection
            -fPIE                        # Position independent executable
        )
        
        add_link_options(
            -Wl,-z,relro                 # Read-only relocations
            -Wl,-z,now                   # Immediate binding
            -pie                         # PIE
        )
    endif()
endif()

# ============================================
# SANITIZERS (Development builds)
# ============================================

option(APOLLO_ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(APOLLO_ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(APOLLO_ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)

if(APOLLO_ENABLE_ASAN)
    message(STATUS "Address Sanitizer enabled")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(APOLLO_ENABLE_TSAN)
    message(STATUS "Thread Sanitizer enabled")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

if(APOLLO_ENABLE_UBSAN)
    message(STATUS "Undefined Behavior Sanitizer enabled")
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

# ============================================
# CODE COVERAGE (Testing builds)
# ============================================

option(APOLLO_ENABLE_COVERAGE "Enable code coverage" OFF)

if(APOLLO_ENABLE_COVERAGE)
    message(STATUS "Code coverage enabled")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
        add_link_options(--coverage)
    endif()
endif()

# ============================================
# STATIC ANALYSIS
# ============================================

option(APOLLO_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)

if(APOLLO_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_PROGRAM clang-tidy)
    if(CLANG_TIDY_PROGRAM)
        message(STATUS "clang-tidy found: ${CLANG_TIDY_PROGRAM}")
        set(CMAKE_CXX_CLANG_TIDY 
            ${CLANG_TIDY_PROGRAM};
            -checks=-*,bugprone-*,performance-*,modernize-*;
            -header-filter=.*apollo/src/.*;
        )
    else()
        message(WARNING "clang-tidy not found")
    endif()
endif()

# ============================================
# PARALLEL BUILD CONFIGURATION
# ============================================

# Set number of parallel jobs for build systems that support it
if(NOT DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
        set(CMAKE_BUILD_PARALLEL_LEVEL ${N} CACHE STRING "Number of parallel build jobs")
        message(STATUS "Parallel build jobs: ${N}")
    endif()
endif()

# ============================================
# DEBUGGING AIDS
# ============================================

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Enable more debug info
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-g3 -ggdb)
    endif()
    
    # Enable assertions
    add_compile_definitions(DEBUG)
else()
    # Disable assertions in release
    add_compile_definitions(NDEBUG)
endif()

# ============================================
# PLATFORM-SPECIFIC OPTIMIZATIONS
# ============================================

if(WIN32)
    # Windows-specific optimizations
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN      # Reduce Windows.h size
        NOMINMAX                 # Don't define min/max macros
    )
    
    if(MSVC)
        # Increase max parallel compilation
        add_compile_options(/MP)
    endif()
endif()

if(UNIX AND NOT APPLE)
    # Linux-specific optimizations
    # Enable gold linker if available (faster than ld)
    option(APOLLO_USE_GOLD_LINKER "Use gold linker" ON)
    if(APOLLO_USE_GOLD_LINKER)
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -fuse-ld=gold -Wl,--version 
                       ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
        if("${LD_VERSION}" MATCHES "GNU gold")
            message(STATUS "Using GNU gold linker")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=gold")
        endif()
    endif()
endif()

# ============================================
# CUSTOM TARGETS
# ============================================

# Clean build artifacts completely
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Removing all build artifacts"
)

# Format code with clang-format
find_program(CLANG_FORMAT_PROGRAM clang-format)
if(CLANG_FORMAT_PROGRAM)
    add_custom_target(format
        COMMAND find ${CMAKE_SOURCE_DIR}/src -name '*.cpp' -o -name '*.h' | 
                xargs ${CLANG_FORMAT_PROGRAM} -i
        COMMENT "Formatting source code"
    )
endif()

# ============================================
# BENCHMARK SUPPORT
# ============================================

option(APOLLO_BUILD_BENCHMARKS "Build performance benchmarks" OFF)

if(APOLLO_BUILD_BENCHMARKS)
    message(STATUS "Building benchmarks")
    enable_testing()
    # Add benchmark subdirectory
    add_subdirectory(benchmarks)
endif()

# ============================================
# USAGE INSTRUCTIONS
# ============================================

message(STATUS "")
message(STATUS "Apollo Optimization Configuration Summary:")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Unity Build: ${APOLLO_UNITY_BUILD}")
message(STATUS "  Precompiled Headers: ${APOLLO_USE_PCH}")
message(STATUS "  ccache: ${APOLLO_USE_CCACHE}")
message(STATUS "  LTO: ${IPO_SUPPORTED}")
message(STATUS "  Hardening: ${APOLLO_ENABLE_HARDENING}")
message(STATUS "")
message(STATUS "Build commands:")
message(STATUS "  Release:    cmake -DCMAKE_BUILD_TYPE=Release ..")
message(STATUS "  Debug:      cmake -DCMAKE_BUILD_TYPE=Debug ..")
message(STATUS "  With ASAN:  cmake -DAPOLLO_ENABLE_ASAN=ON ..")
message(STATUS "  Benchmarks: cmake -DAPOLLO_BUILD_BENCHMARKS=ON ..")
message(STATUS "")
