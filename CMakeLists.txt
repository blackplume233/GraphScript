cmake_minimum_required(VERSION 3.20)
project(GraphScript VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GS_BUILD_TESTS "Build GraphScript unit tests" ON)
option(GS_BUILD_CLI   "Build gs CLI tool"            ON)

include(FetchContent)

# ─── Tree-sitter runtime and AI Native asset grammar ────────────────
FetchContent_Declare(
    tree_sitter_runtime
    URL https://registry.npmjs.org/tree-sitter/-/tree-sitter-0.21.1.tgz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(tree_sitter_runtime)

add_library(tree_sitter_runtime STATIC
    ${tree_sitter_runtime_SOURCE_DIR}/vendor/tree-sitter/lib/src/lib.c
)
target_include_directories(tree_sitter_runtime PUBLIC
    ${tree_sitter_runtime_SOURCE_DIR}/vendor/tree-sitter/lib/include
    ${tree_sitter_runtime_SOURCE_DIR}/vendor/tree-sitter/lib/src
)

add_library(tree_sitter_graphscript_asset STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/tree-sitter-graphscript-asset/src/parser.c
)
target_include_directories(tree_sitter_graphscript_asset PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/tree-sitter-graphscript-asset/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/tree-sitter-graphscript-asset/bindings/c
)

# ─── Core Library ───────────────────────────────────────────────────
file(GLOB_RECURSE GS_CORE_SOURCES src/*.cpp)
file(GLOB_RECURSE GS_CORE_HEADERS include/*.h)

add_library(graphscript_core STATIC ${GS_CORE_SOURCES} ${GS_CORE_HEADERS})

target_include_directories(graphscript_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(graphscript_core PUBLIC cxx_std_17)
target_link_libraries(graphscript_core PUBLIC tree_sitter_runtime tree_sitter_graphscript_asset)

if(MSVC)
    target_compile_options(graphscript_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(graphscript_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ─── cpp-httplib (header-only HTTP server) ─────────────────────────
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)

# ─── CLI Tool ───────────────────────────────────────────────────────
if(GS_BUILD_CLI)
    FetchContent_MakeAvailable(httplib)

    add_executable(gs cli/main.cpp cli/editor.cpp cli/server.cpp cli/source_diagnostics.cpp)
    target_include_directories(gs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cli)
    target_link_libraries(gs PRIVATE graphscript_core httplib::httplib)
    if(MSVC)
        target_compile_options(gs PRIVATE /utf-8)
    endif()

    target_compile_definitions(gs PRIVATE
        GS_WEB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/webapp/dist"
        GS_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets"
    )
endif()

# ─── Tests ──────────────────────────────────────────────────────────
if(GS_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    file(GLOB_RECURSE GS_TEST_SOURCES tests/*.cpp)

    add_executable(gs_tests ${GS_TEST_SOURCES})
    target_sources(gs_tests PRIVATE cli/editor.cpp cli/source_diagnostics.cpp)
    target_include_directories(gs_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cli)
    target_link_libraries(gs_tests PRIVATE graphscript_core GTest::gtest_main)
    if(MSVC)
        target_compile_options(gs_tests PRIVATE /utf-8)
    endif()

    target_compile_definitions(gs_tests PRIVATE
        GS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        GS_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets"
    )

    include(GoogleTest)
    gtest_discover_tests(gs_tests)
endif()
