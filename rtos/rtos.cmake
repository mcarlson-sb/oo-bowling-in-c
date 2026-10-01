# The RTOS side, on the port OO_C_PORT names: FreeRTOS-Kernel built for that port, the port's own
# component, rtos/port/<port>, and the RTOS components, each a library under the same warnings.
#   - posix: the POSIX port, on a Linux host, for the integration tests (CMakeLists.txt);
#   - cm4f: the ARM_CM4F port, for the target images (target/target.cmake), whose device library
#     gives FreeRTOSConfig.h its device.h.
# Included from the top-level CMakeLists.txt, so its paths are the project's.

# FreeRTOS-Kernel, fetched like GoogleTest, and built here from the files the host needs: its own
# CMake isn't used (SOURCE_SUBDIR names a directory it doesn't have). Vendor code, so it builds
# without the library's warnings.
FetchContent_Declare(
    freertos_kernel
    URL https://github.com/FreeRTOS/FreeRTOS-Kernel/archive/refs/tags/V11.2.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR not-its-cmake
)
FetchContent_MakeAvailable(freertos_kernel)
set(FREERTOS_DIR ${freertos_kernel_SOURCE_DIR})
if(OO_C_PORT STREQUAL "posix")
    set(FREERTOS_PORT_DIR ${FREERTOS_DIR}/portable/ThirdParty/GCC/Posix)
    set(FREERTOS_PORT_SOURCES ${FREERTOS_PORT_DIR}/port.c ${FREERTOS_PORT_DIR}/utils/wait_for_event.c)
    set(FREERTOS_PORT_INCLUDES ${FREERTOS_PORT_DIR} ${FREERTOS_PORT_DIR}/utils)
elseif(OO_C_PORT STREQUAL "cm4f")
    set(FREERTOS_PORT_DIR ${FREERTOS_DIR}/portable/GCC/ARM_CM4F)
    set(FREERTOS_PORT_SOURCES ${FREERTOS_PORT_DIR}/port.c)
    set(FREERTOS_PORT_INCLUDES ${FREERTOS_PORT_DIR})
else()
    message(FATAL_ERROR "OO_C_PORT is posix or cm4f, not '${OO_C_PORT}'")
endif()
add_library(freertos STATIC
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_PORT_SOURCES}
    # The kernel's hooks for its own static memory: it calls them, so they link with it.
    rtos/port/rtos_memory.c
)
set_source_files_properties(rtos/port/rtos_memory.c PROPERTIES COMPILE_OPTIONS "${OO_C_WARNINGS}")
target_include_directories(freertos SYSTEM PUBLIC
    rtos/port/${OO_C_PORT}/include ${FREERTOS_DIR}/include ${FREERTOS_PORT_INCLUDES})
target_link_libraries(freertos PUBLIC support) # configASSERT: Fault_Stop
if(OO_C_PORT STREQUAL "posix")
    target_compile_definitions(freertos PRIVATE _GNU_SOURCE)
    find_package(Threads REQUIRED)
    target_link_libraries(freertos PUBLIC Threads::Threads)
else()
    target_link_libraries(freertos PUBLIC device) # FreeRTOSConfig.h: the image's device.h
endif()

# The port's component: where a task's stack ends, and the host's stack budgets on the port.
oo_component(port_${OO_C_PORT} rtos/port/${OO_C_PORT}
    SOURCES rtos/port/${OO_C_PORT}/port_stack.c
    USES freertos)
oo_component(task_stack rtos/task_stack
    SOURCES rtos/task_stack/task_stack.c
    USES port_${OO_C_PORT})
oo_component(router rtos/router
    SOURCES rtos/router/router.c
    USES protocol freertos)
# The interrupt side's files have the interrupt's own stack tripwire.
oo_component(pinsetter rtos/pinsetter
    SOURCES rtos/pinsetter/pinsetter.c rtos/pinsetter/pinsetter_isr.c
    USES protocol freertos)
# The host allocates every actor's instance, so it sees their state; its users see only its
# public header, which names the kinds the router binds, the game it hosts and the port's budgets.
oo_component(host rtos/host
    SOURCES rtos/host/actor_host.c rtos/host/actor_host_isr.c
    USES router game_actor port_${OO_C_PORT} freertos)
target_link_libraries(host PRIVATE game_actor_state scoreboard_state running_average_state
                                   pinsetter task_stack support)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(rtos/pinsetter/pinsetter_isr.c rtos/host/actor_host_isr.c
        PROPERTIES COMPILE_OPTIONS -Wstack-usage=96)
endif()
