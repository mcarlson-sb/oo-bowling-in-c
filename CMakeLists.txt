cmake_minimum_required(VERSION 3.20)
project(oo_in_c LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OO_C_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
set(OO_TEST_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)

# Second pass over the same tests with undefined-behavior checks. Trap mode needs no
# runtime library, so it works with MinGW, which ships no libubsan or libasan.
option(OO_C_SANITIZE "Build with -fsanitize=undefined (trap mode)" OFF)
if(OO_C_SANITIZE)
    add_compile_options(-fsanitize=undefined -fsanitize-trap=undefined -fno-omit-frame-pointer -O0 -g)
    add_link_options(-fsanitize=undefined -fsanitize-trap=undefined)
endif()

# ---- Library under test ---------------------------------------------------
add_library(bowling STATIC
    src/frame.c
    src/frame_context.c
    src/game.c
    src/regular_frame.c
    src/roll_list.c
    src/spare_frame.c
    src/strike_frame.c
    src/tenth_frame.c
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
    test/roll_list_test.cpp
    test/game_test.cpp
)
target_link_libraries(bowling_tests PRIVATE bowling GTest::gtest_main)
# roll_list_test.cpp tests a private type directly, so it needs the private headers.
target_include_directories(bowling_tests PRIVATE src)
target_compile_options(bowling_tests PRIVATE ${OO_TEST_WARNINGS})

include(GoogleTest)
gtest_discover_tests(bowling_tests)

# ---- Coverage (gcov) ------------------------------------------------------
# Instruments the library only, so GoogleTest's own code stays out of the report.
# `cmake --build <dir> --target coverage` clears old data, runs the tests and prints a
# gcov summary per source file. See the README's Coverage section for which gaps to expect.
option(OO_C_COVERAGE "Instrument the library for gcov coverage" OFF)
if(OO_C_COVERAGE)
    target_compile_options(bowling PRIVATE --coverage -O0)
    target_link_options(bowling_tests PRIVATE --coverage)

    find_program(GCOV_EXECUTABLE gcov REQUIRED)
    # Where CMake puts the library's object files, and so gcov's data files.
    set(COVERAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/bowling.dir/src)
    get_target_property(BOWLING_SOURCES bowling SOURCES)
    set(COVERAGE_DATA "")
    foreach(source IN LISTS BOWLING_SOURCES)
        get_filename_component(name ${source} NAME)
        list(APPEND COVERAGE_DATA ${name}.gcda)
    endforeach()

    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E rm -f ${COVERAGE_DATA}
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND ${GCOV_EXECUTABLE} -b -c ${COVERAGE_DATA}
        WORKING_DIRECTORY ${COVERAGE_DIR}
        DEPENDS bowling_tests
        COMMENT "Running the tests with coverage, then gcov"
        VERBATIM)
endif()
