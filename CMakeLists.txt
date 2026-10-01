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

# Data races between the RTOS shell's tasks and its interrupt side. ThreadSanitizer needs its
# runtime library, which MinGW doesn't ship, so this build runs in CI on Linux.
option(OO_C_TSAN "Build with -fsanitize=thread" OFF)
if(OO_C_TSAN)
    add_compile_options(-fsanitize=thread -O1 -g)
    add_link_options(-fsanitize=thread)
endif()

# ---- Library under test ---------------------------------------------------
# Each component's public headers are in its include/; its private ones sit beside its sources.
set(OO_C_PUBLIC_INCLUDES
    src/value_objects/include
    src/scorer/include
    src/protocol/include
    src/actors/game/include
    src/actors/scoreboard/include
    src/actors/running_average/include
    src/support/include
)
# The components' private headers: the scorer's arithmetic, and each actor's state and parts.
set(OO_C_PRIVATE_INCLUDES
    src/scorer
    src/actors/game
    src/actors/scoreboard
    src/actors/running_average
)
add_library(bowling STATIC
    src/support/fault.c
    src/scorer/roll_edit.c
    src/scorer/scorer.c
    src/value_objects/rules.c
    src/actors/game/game_actor.c
    src/actors/game/game_protocol.c
    src/protocol/outbox.c
    src/protocol/message.c
    src/actors/scoreboard/scoreboard.c
    src/value_objects/frame_board.c
    src/actors/game/held_rolls.c
    src/actors/running_average/running_average.c
    src/actors/game/subscribers.c
)
target_include_directories(bowling PUBLIC ${OO_C_PUBLIC_INCLUDES} PRIVATE ${OO_C_PRIVATE_INCLUDES})
target_compile_options(bowling PRIVATE ${OO_C_WARNINGS})

# Stack tripwires (ENG-1.3): -Wstack-usage warns, and -Werror fails the build, on any function
# whose frame is over the limit. Host numbers: a tripwire for regressions, not a target budget.
# The limits have headroom over the largest frames today, on CI's GCC 13 (the gate) and on
# GCC 16, which builds here too:
#   - everything but the interrupt side: 320 bytes, over the largest frame today, Scorer_Edit's.
#   - the interrupt side's own files, rtos/pinsetter/pinsetter_isr.c and
#     rtos/host/game_shell_isr.c: their own limit, with the RTOS shell.
set(OO_C_STACK_LIMIT 320)
# GCC's option: clang has no -Wstack-usage. Every build that gates on it uses GCC; clang only
# builds the mutation-testing configuration below.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(bowling PRIVATE -Wstack-usage=${OO_C_STACK_LIMIT})
endif()

# Mutation testing with Mull (tools/mull.yml, tools/mutation.sh): clang compiles the library
# through Mull's plugin, which builds every mutant into it; mull-runner then runs the tests once
# per mutant. The tests themselves aren't instrumented. Needs clang and Mull of the same LLVM
# version (Mull 0.34.1 for LLVM 18 on Ubuntu 24.04).
option(OO_C_MUTATION "Build the library with Mull's mutants in it (clang only)" OFF)
set(OO_C_MULL_PLUGIN "/usr/lib/mull-ir-frontend-18" CACHE FILEPATH "Mull's clang plugin")
if(OO_C_MUTATION)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "OO_C_MUTATION needs clang, the compiler Mull's plugin loads into")
    endif()
    target_compile_options(bowling PRIVATE -fpass-plugin=${OO_C_MULL_PLUGIN} -g -grecord-command-line)
endif()

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
    test/scorer/scorer_test.cpp
    test/support/fault_test.cpp
    test/actors/game/game_actor_test.cpp
    test/actors/scoreboard/scoreboard_actor_test.cpp
    test/actors/running_average/running_average_test.cpp
    test/protocol/outbox_test.cpp
    test/protocol/protocol_test.cpp
)
target_link_libraries(bowling_tests PRIVATE bowling GTest::gtest_main)
# The actors' state headers: the tests see inside, as the shell does. And the tests' fixtures.
target_include_directories(bowling_tests PRIVATE ${OO_C_PRIVATE_INCLUDES} test/fixtures)
target_compile_options(bowling_tests PRIVATE ${OO_TEST_WARNINGS})

include(GoogleTest)
gtest_discover_tests(bowling_tests)

# The components' public include paths, as compiler flags, for the probes below.
set(OO_C_PUBLIC_INCLUDE_FLAGS "")
foreach(dir IN LISTS OO_C_PUBLIC_INCLUDES)
    list(APPEND OO_C_PUBLIC_INCLUDE_FLAGS -I ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
endforeach()

# Each actor's state is its own: with the public include paths alone, the compiler must refuse
# to allocate one, for want of its definition. Fails if a state struct moves into an include/.
add_test(NAME actor_state_is_hidden
         COMMAND ${CMAKE_C_COMPILER} -std=c11 -fsyntax-only ${OO_C_PUBLIC_INCLUDE_FLAGS}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test/probes/hidden_state_probe.c)
# In the C locale, so GCC quotes with ASCII quotes; matched loosely all the same. One pattern for
# GCC's wording and one for clang's (the mutation build's compiler), each requiring all three.
set_tests_properties(actor_state_is_hidden PROPERTIES ENVIRONMENT "LC_ALL=C"
    PASS_REGULAR_EXPRESSION
    "size of [^ ]*game[^ ]* isn.t known.*size of [^ ]*board[^ ]* isn.t known.*size of [^ ]*average[^ ]* isn.t known;GameActor[^
]*never completed.*Scoreboard[^
]*never completed.*RunningAverage[^
]*never completed")

# The protocol depends on the rules and frames, not on the scorer that plays them: with the
# protocol's and the observers' headers included, the scorer's type must be unknown.
add_test(NAME protocol_is_free_of_the_scorer
         COMMAND ${CMAKE_C_COMPILER} -std=c11 -fsyntax-only ${OO_C_PUBLIC_INCLUDE_FLAGS}
                 -I ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/scoreboard
                 -I ${CMAKE_CURRENT_SOURCE_DIR}/src/actors/running_average
                 ${CMAKE_CURRENT_SOURCE_DIR}/test/probes/scorer_free_probe.c)
set_tests_properties(protocol_is_free_of_the_scorer PROPERTIES ENVIRONMENT "LC_ALL=C"
    PASS_REGULAR_EXPRESSION "unknown type name .Scorer.")

# ---- The RTOS shell (FreeRTOS on its POSIX port) ----------------------------
# The POSIX port runs on Linux only, so this is on there (WSL, CI) and off elsewhere; the Windows
# builds keep testing the pure core. Its integration tests are an executable of their own: each
# starts the scheduler, which is state for the whole process.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(OO_C_RTOS_DEFAULT ON)
else()
    set(OO_C_RTOS_DEFAULT OFF)
endif()
option(OO_C_RTOS "Build the FreeRTOS shell and its integration tests (POSIX port, Linux)"
       ${OO_C_RTOS_DEFAULT})
if(OO_C_RTOS)
    # FreeRTOS-Kernel, fetched like GoogleTest, and built here from the files the shell needs:
    # its own CMake isn't used (SOURCE_SUBDIR names a directory it doesn't have). Vendor code,
    # so it builds without the library's warnings.
    FetchContent_Declare(
        freertos_kernel
        URL https://github.com/FreeRTOS/FreeRTOS-Kernel/archive/refs/tags/V11.2.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR not-its-cmake
    )
    FetchContent_MakeAvailable(freertos_kernel)
    set(FREERTOS_DIR ${freertos_kernel_SOURCE_DIR})
    set(FREERTOS_PORT_DIR ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix)
    add_library(freertos STATIC
        ${FREERTOS_DIR}/tasks.c
        ${FREERTOS_DIR}/queue.c
        ${FREERTOS_DIR}/list.c
        ${FREERTOS_PORT_DIR}/port.c
        ${FREERTOS_PORT_DIR}/utils/wait_for_event.c
        # The kernel's hooks for its own static memory: it calls them, so they link with it.
        rtos/port/posix/rtos_memory.c
    )
    set_source_files_properties(rtos/port/posix/rtos_memory.c PROPERTIES
        COMPILE_OPTIONS "${OO_C_WARNINGS}")
    target_include_directories(freertos SYSTEM PUBLIC
        rtos/port/posix/include ${FREERTOS_DIR}/include ${FREERTOS_PORT_DIR}
        ${FREERTOS_PORT_DIR}/utils)
    target_compile_definitions(freertos PRIVATE _GNU_SOURCE)
    find_package(Threads REQUIRED)
    target_link_libraries(freertos PUBLIC Threads::Threads bowling) # bowling: Fault_Stop

    # The RTOS shell: everything that touches FreeRTOS, under the library's warnings.
    add_library(rtos_shell STATIC
        rtos/host/game_shell.c
        rtos/host/game_shell_isr.c
        rtos/pinsetter/pinsetter.c
        rtos/pinsetter/pinsetter_isr.c
        rtos/router/router.c
        rtos/port/posix/posix_stack.c
    )
    # The actors' state headers: the shell hosts the instances, so it sees inside.
    target_include_directories(rtos_shell
        PUBLIC rtos/host/include rtos/router/include rtos/pinsetter/include rtos/port/posix/include
        PRIVATE ${OO_C_PRIVATE_INCLUDES})
    target_compile_options(rtos_shell PRIVATE ${OO_C_WARNINGS})
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(rtos_shell PRIVATE -Wstack-usage=${OO_C_STACK_LIMIT})
        # The shell's interrupt side: the pinsetter's interrupt is 64 bytes in every gated build
        # on GCC 13 (debug, release, UBSan, TSan, coverage), so one limit over it.
        set_source_files_properties(rtos/pinsetter/pinsetter_isr.c rtos/host/game_shell_isr.c
            PROPERTIES COMPILE_OPTIONS -Wstack-usage=96)
    endif()
    target_link_libraries(rtos_shell PUBLIC freertos bowling)

    add_executable(rtos_tests
        test/rtos/port/posix/rtos_skeleton_test.cpp
        test/rtos/host/game_shell_test.cpp
    )
    target_include_directories(rtos_tests PRIVATE test/fixtures)
    target_link_libraries(rtos_tests PRIVATE rtos_shell GTest::gtest_main)
    target_compile_options(rtos_tests PRIVATE ${OO_TEST_WARNINGS})
    if(OO_C_TSAN)
        # The POSIX port's own synchronization is invisible to ThreadSanitizer; see the file.
        gtest_discover_tests(rtos_tests PROPERTIES ENVIRONMENT
            "TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tools/tsan-freertos.supp")
    else()
        gtest_discover_tests(rtos_tests)
    endif()
endif()

# ---- Coverage (gcov) ------------------------------------------------------
# Instruments the library only, so GoogleTest's own code stays out of the report.
# `cmake --build <dir> --target coverage` clears old data, runs the tests and prints a
# gcov summary per source file. See the README's Coverage section for which gaps to expect.
option(OO_C_COVERAGE "Instrument the library for gcov coverage" OFF)
if(OO_C_COVERAGE)
    target_compile_options(bowling PRIVATE --coverage -O0)
    target_link_options(bowling_tests PRIVATE --coverage)
    # A death test's child aborts before gcov saves its data. This Fault_Stop saves it first;
    # defined in the test executable, it keeps the library's fault.c out of the link.
    target_sources(bowling_tests PRIVATE test/support/coverage_fault_stop.c)

    find_program(GCOV_EXECUTABLE gcov REQUIRED)
    # gcov's data files, beside the library's object files: one for each source's path.
    set(COVERAGE_DIR ${CMAKE_CURRENT_BINARY_DIR})
    get_target_property(BOWLING_SOURCES bowling SOURCES)
    list(REMOVE_ITEM BOWLING_SOURCES src/support/fault.c) # not linked: see coverage_fault_stop.c
    set(COVERAGE_DATA "")
    foreach(source IN LISTS BOWLING_SOURCES)
        list(APPEND COVERAGE_DATA ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/bowling.dir/${source}.gcda)
    endforeach()

    # Every test executable ctest runs must be built first, or ctest fails on the missing one.
    set(COVERAGE_TEST_TARGETS bowling_tests)
    if(TARGET rtos_tests)
        list(APPEND COVERAGE_TEST_TARGETS rtos_tests)
        # The shell is measured too; rtos_tests links it and the instrumented library.
        target_compile_options(rtos_shell PRIVATE --coverage -O0)
        target_link_options(rtos_tests PRIVATE --coverage)
        get_target_property(SHELL_SOURCES rtos_shell SOURCES)
        foreach(source IN LISTS SHELL_SOURCES)
            list(APPEND COVERAGE_DATA
                ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/rtos_shell.dir/${source}.gcda)
        endforeach()
    endif()
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E rm -f ${COVERAGE_DATA}
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND ${GCOV_EXECUTABLE} -b -c ${COVERAGE_DATA}
        WORKING_DIRECTORY ${COVERAGE_DIR}
        DEPENDS ${COVERAGE_TEST_TARGETS}
        COMMENT "Running the tests with coverage, then gcov"
        VERBATIM)
endif()
