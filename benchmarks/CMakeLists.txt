cmake_minimum_required(VERSION 3.20)

project(apollo_benchmarks)

add_executable(apollo_benchmarks
    benchmark_hotpaths.cpp
)

target_include_directories(apollo_benchmarks PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
)

set_target_properties(apollo_benchmarks PROPERTIES CXX_STANDARD 23)
