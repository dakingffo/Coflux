cmake_minimum_required(VERSION 3.14)
project(Coflux VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build (Debug, Release, ...)" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Coflux库:header only
# ==============================================================================
add_library(coflux INTERFACE)
add_library(Coflux::coflux ALIAS coflux)

target_compile_features(coflux INTERFACE cxx_std_20)
target_include_directories(coflux INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 可选的构建目标
# ==============================================================================

option(COFLUX_BUILD_TESTS "Build the unit tests for Coflux." ON)
option(COFLUX_BUILD_BENCHMARKS "Build the benchmark tests for Coflux." ON)
option(COFLUX_BUILD_EXAMPLES "Build the example programs for Coflux." ON)

include(FetchContent)

# 单元测试
if(COFLUX_BUILD_TESTS)
    message(STATUS "Unit tests are enabled for Coflux.")
    enable_testing()

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        FIND_PACKAGE_ARGS NAMES GTest CONFIG
    )
    FetchContent_MakeAvailable(googletest)
    file(GLOB TESTS "tests/*.cpp")
    add_executable(coflux_tests ${TESTS})
    target_link_libraries(coflux_tests PRIVATE coflux GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(coflux_tests)
endif()

# 性能基准测试
# ==============================================================================
if(COFLUX_BUILD_BENCHMARKS)
    message(STATUS "Benchmarks are enabled for Coflux.")

    if(NOT COFLUX_BUILD_TESTS)
        set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
    endif()

    message(STATUS "Using integrated dependency handling for benchmark")

    set(BENCHMARK_ENABLE_TESTING OFF)
    FetchContent_Declare(
        googlebenchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
        FIND_PACKAGE_ARGS NAMES benchmark CONFIG
    )
    FetchContent_MakeAvailable(googlebenchmark)

    add_executable(coflux_benchmarks_noop_creation "benchmarks/bench_noop_creation.cpp")
    target_link_libraries(coflux_benchmarks_noop_creation PRIVATE coflux benchmark::benchmark_main)
    add_executable(coflux_benchmarks_thread_pool_creation "benchmarks/bench_thread_pool_creation.cpp")
    target_link_libraries(coflux_benchmarks_thread_pool_creation PRIVATE coflux benchmark::benchmark_main)
    add_executable(coflux_benchmarks_pipeline "benchmarks/bench_pipeline.cpp")
    target_link_libraries(coflux_benchmarks_pipeline PRIVATE coflux benchmark::benchmark_main)
    add_executable(coflux_benchmarks_channel "benchmarks/bench_channel.cpp")
    target_link_libraries(coflux_benchmarks_channel PRIVATE coflux benchmark::benchmark_main)
endif()


# 示例程序
# ==============================================================================
if(COFLUX_BUILD_EXAMPLES)
    message(STATUS "Examples are enabled for Coflux.")
    file(GLOB EXAMPLES "examples/*.cpp")
    add_executable(coflux_examples ${EXAMPLES})
    target_link_libraries(coflux_examples PRIVATE coflux)
endif()


# 欢迎程序
# ==============================================================================
message(STATUS "Hello Coflux!")
    
add_executable(coflux_source src/main.cpp)
target_link_libraries(coflux_source PRIVATE coflux)


# 安装规则
# ==============================================================================
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS coflux EXPORT CofluxTargets)

# header
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/CofluxConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

export(EXPORT CofluxTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/CofluxTargets.cmake"
    NAMESPACE Coflux::
)

install(EXPORT CofluxTargets
    FILE CofluxTargets.cmake
    NAMESPACE Coflux::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Coflux
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/CofluxConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/CofluxConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Coflux
)
