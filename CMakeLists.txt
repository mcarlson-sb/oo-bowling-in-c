cmake_minimum_required(VERSION 3.20)
project(oo_in_c LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OO_C_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)

# ---- Library under test ---------------------------------------------------
add_library(bowling STATIC
    src/game.c
)
target_include_directories(bowling PUBLIC include PRIVATE src)
target_compile_options(bowling PRIVATE ${OO_C_WARNINGS})

# ---- Host unit tests (GoogleTest) -----------------------------------------
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(bowling_tests
    test/game_test.cpp
)
target_link_libraries(bowling_tests PRIVATE bowling GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(bowling_tests)
