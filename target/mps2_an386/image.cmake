# The QEMU image, mps2-an386: its board, and the executables that run on it under
# qemu-system-arm. Included from target/target.cmake, so its paths are the project's.

# The board: startup, semihosting and the fail-stop, as objects, so that the vector table is kept
# and this Fault_Stop, not the host's in src/support, is the one linked.
add_library(mps2_an386_board OBJECT
    target/mps2_an386/startup.c
    target/mps2_an386/semihosting.c
    target/mps2_an386/fault_stop.c
    target/newlib/assert_func.c)
target_include_directories(mps2_an386_board PUBLIC target/mps2_an386/include)
target_link_libraries(mps2_an386_board PUBLIC device support)
target_compile_options(mps2_an386_board PRIVATE ${OO_C_WARNINGS})

set(OO_C_QEMU_COMMAND qemu-system-arm -machine mps2-an386 -cpu cortex-m4 -nographic -monitor none
    -serial none -semihosting-config enable=on,target=native -kernel)

# An executable for the QEMU image: linked by its script, with a map file beside it.
function(oo_mps2_executable name)
    set_target_properties(${name} PROPERTIES SUFFIX .elf)
    target_link_libraries(${name} PRIVATE mps2_an386_board)
    target_link_options(${name} PRIVATE
        -T${CMAKE_CURRENT_SOURCE_DIR}/target/mps2_an386/mps2_an386.ld
        -Wl,-Map=$<TARGET_FILE_DIR:${name}>/${name}.map)
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/target/mps2_an386/mps2_an386.ld)
endfunction()

# The pure core's tests on the target: the host's own sources, through the GoogleTest shim in
# test/target, whose gtest/gtest.h they include. Its heap and exit are test_system.c's, and its
# main stack is large enough for any test's locals.
add_executable(core_tests ${OO_C_CORE_TEST_SOURCES} test/target/gtest_shim.cpp
                          test/target/test_system.c)
oo_mps2_executable(core_tests)
target_include_directories(core_tests SYSTEM PRIVATE test/target)
target_include_directories(core_tests PRIVATE test/fixtures)
target_link_libraries(core_tests PRIVATE game_actor_state scoreboard_state running_average_state)
target_compile_options(core_tests PRIVATE ${OO_TEST_WARNINGS})
target_link_options(core_tests PRIVATE --specs=nosys.specs -Wl,--defsym=__main_stack_size=0x8000)

add_test(NAME core_tests_on_qemu COMMAND ${OO_C_QEMU_COMMAND} $<TARGET_FILE:core_tests>)
set_tests_properties(core_tests_on_qemu PROPERTIES TIMEOUT 300 LABELS qemu)

# The firmware on QEMU: the XMC4500's actors (target/firmware), its pinsetters driven by the
# CMSDK timer's interrupt, and the smoke test's league task. The interrupt's file has the
# interrupt side's stack tripwire.
add_executable(mps2_an386_firmware target/mps2_an386/firmware_main.c
                                   target/mps2_an386/pinsetter_timer.c)
oo_mps2_executable(mps2_an386_firmware)
target_compile_options(mps2_an386_firmware PRIVATE ${OO_C_WARNINGS})
set_source_files_properties(target/mps2_an386/pinsetter_timer.c PROPERTIES
    COMPILE_OPTIONS -Wstack-usage=96)
target_link_libraries(mps2_an386_firmware PRIVATE firmware)
target_link_options(mps2_an386_firmware PRIVATE -Wl,--print-memory-usage)
oo_firmware_checks(mps2_an386_firmware target/mps2_an386/mps2_an386.ld)

add_test(NAME smoke_on_qemu COMMAND ${OO_C_QEMU_COMMAND} $<TARGET_FILE:mps2_an386_firmware>)
set_tests_properties(smoke_on_qemu PROPERTIES TIMEOUT 120 LABELS qemu
                     PASS_REGULAR_EXPRESSION "SMOKE: PASSED")

# The early timing estimate: the worst-case message and a QUERY_FIGURE, counted in instructions
# under -icount shift=0, where QEMU's virtual clock advances one nanosecond an instruction.
add_executable(timing target/mps2_an386/timing_main.c)
oo_mps2_executable(timing)
target_compile_options(timing PRIVATE ${OO_C_WARNINGS})
target_link_libraries(timing PRIVATE game_actor_state scoreboard_state)
add_test(NAME timing_on_qemu
         COMMAND qemu-system-arm -machine mps2-an386 -cpu cortex-m4 -nographic -monitor none
                 -serial none -semihosting-config enable=on,target=native
                 -icount shift=0,align=off,sleep=off -kernel $<TARGET_FILE:timing>)
set_tests_properties(timing_on_qemu PROPERTIES TIMEOUT 120 LABELS qemu)
