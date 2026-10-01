# The target's toolchain: GNU Arm Embedded (arm-none-eabi-gcc) for a Cortex-M4F, with the FPU's
# hard-float calling convention. Both images use it, the XMC4500's and QEMU's mps2-an386, and
# OO_C_TARGET picks which (target/CMakeLists.txt).
#   cmake -S . -B build-mps2 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-cm4f.cmake \
#         -DOO_C_TARGET=mps2_an386
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m4)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
# No operating system to run a test program on: the compiler checks build a library instead.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(OO_C_CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
# Each function and object in a section of its own, so the linker can drop what nothing uses.
set(CMAKE_C_FLAGS_INIT "${OO_C_CPU_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "${OO_C_CPU_FLAGS}")
# newlib-nano, with no system calls behind it: anything that pulls one in (printf's _write,
# malloc's _sbrk) fails to link. Each image brings its own startup.
set(CMAKE_EXE_LINKER_FLAGS_INIT "${OO_C_CPU_FLAGS} --specs=nano.specs -nostartfiles -Wl,--gc-sections")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
