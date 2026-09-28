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

# Data races between the pinsetter's interrupt side and the main loop. ThreadSanitizer needs
# its runtime library, which MinGW doesn't ship, so this build runs in CI on Linux.
option(OO_C_TSAN "Build with -fsanitize=thread" OFF)
if(OO_C_TSAN)
    add_compile_options(-fsanitize=thread -O1 -g)
    add_link_options(-fsanitize=thread)
endif()

# ---- Library under test ---------------------------------------------------
add_library(bowling STATIC
    src/fault.c
    src/frame.c
    src/frame_context.c
    src/frame_listeners.c
    src/frame_reporter.c
    src/game.c
    src/pinsetter.c
    src/pinsetter_isr.c
    src/regular_frame.c
    src/roll_list.c
    src/roll_edit.c
    src/roll_log.c
    src/scorecard.c
    src/slot_pool.c
    src/spare_frame.c
    src/strike_frame.c
    src/tenth_frame.c
)
target_include_directories(bowling PUBLIC include PRIVATE src)
target_compile_options(bowling PRIVATE ${OO_C_WARNINGS})

# Stack tripwires (ENG-1.3): -Wstack-usage warns, and -Werror fails the build, on any function
# whose frame is over the limit. Host numbers: a tripwire for regressions, not a target budget.
# The limits have headroom over the largest frames today, on CI's GCC 13 (the gate) and on
# GCC 16, which builds here too:
#   - everything but the interrupt side: 320 bytes, over Game_CorrectRoll's and
#     Game_EditRolls's 224.
#   - the interrupt side's own file, pinsetter_isr.c: a limit per build type, because GCC's
#     limit is per file and its frames differ so much between builds. Pinsetter_Post is 8 bytes
#     in release (both compilers); 80 (GCC 13) or 96 (GCC 16) under ThreadSanitizer; at -O0,
#     where Pinsetter_Enqueue isn't inlined, Enqueue is 96 or 128. Coverage builds are -O0.
set(OO_C_STACK_LIMIT 320)
if(OO_C_TSAN)
    set(OO_C_INTERRUPT_STACK_LIMIT 128)
elseif(OO_C_COVERAGE)
    set(OO_C_INTERRUPT_STACK_LIMIT 160)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(OO_C_INTERRUPT_STACK_LIMIT 32)
else()
    set(OO_C_INTERRUPT_STACK_LIMIT 160)
endif()
target_compile_options(bowling PRIVATE -Wstack-usage=${OO_C_STACK_LIMIT})
set_source_files_properties(src/pinsetter_isr.c PROPERTIES
    COMPILE_OPTIONS -Wstack-usage=${OO_C_INTERRUPT_STACK_LIMIT})

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
    test/frame_test.cpp
    test/roll_list_test.cpp
    test/roll_log_test.cpp
    test/slot_pool_test.cpp
    test/frame_listeners_test.cpp
    test/game_test.cpp
    test/nine_pin_no_tap_test.cpp
    test/scoreboard_test.cpp
    test/correction_test.cpp
    test/pinsetter_test.cpp
    test/remote_scoreboard_test.cpp
    test/pinsetter_overlap_test.cpp
)
find_package(Threads REQUIRED) # the pinsetter test runs a second thread
target_link_libraries(bowling_tests PRIVATE bowling GTest::gtest_main Threads::Threads)
# frame_test.cpp, roll_list_test.cpp, roll_log_test.cpp, slot_pool_test.cpp and
# pinsetter_overlap_test.cpp test private types or hooks directly, so they need the private
# headers.
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
