# The XMC4500 firmware image, for the bike's main board: built here and in CI, and run only on
# the hardware. Included from target/target.cmake, so its paths are the project's.

# The board: Infineon's startup and clock setup, vendor code under its own flags, and this
# image's fail-stop, as objects, so that the vector table is kept and this Fault_Stop, not the
# host's in src/support, is the one linked. The startup skips newlib's constructor table, which
# needs the C runtime's start files: the firmware is C, with no constructors.
add_library(xmc4500_board OBJECT
    ${XMC_DIR}/startup_XMC4500.S
    ${XMC_DIR}/system_XMC4500.c
    target/xmc4500/fault_stop.c
    target/newlib/assert_func.c)
set_source_files_properties(${XMC_DIR}/startup_XMC4500.S PROPERTIES
    COMPILE_DEFINITIONS __SKIP_LIBC_INIT_ARRAY)
set_source_files_properties(target/xmc4500/fault_stop.c target/newlib/assert_func.c PROPERTIES
    COMPILE_OPTIONS "${OO_C_WARNINGS}")
target_link_libraries(xmc4500_board PUBLIC device support)

add_executable(xmc4500_firmware target/xmc4500/main.c)
set_target_properties(xmc4500_firmware PROPERTIES SUFFIX .elf)
target_compile_options(xmc4500_firmware PRIVATE ${OO_C_WARNINGS})
target_link_libraries(xmc4500_firmware PRIVATE xmc4500_board firmware)
target_link_options(xmc4500_firmware PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/target/xmc4500/xmc4500.ld
    -Wl,-Map=$<TARGET_FILE_DIR:xmc4500_firmware>/xmc4500_firmware.map
    -Wl,--print-memory-usage)
set_property(TARGET xmc4500_firmware APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/target/xmc4500/xmc4500.ld)
oo_firmware_checks(xmc4500_firmware target/xmc4500/xmc4500.ld)
