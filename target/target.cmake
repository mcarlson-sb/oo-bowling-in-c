# A target build: the components, the RTOS side on the Cortex-M4F port, and one image, chosen by
# OO_C_TARGET. Included from the top-level CMakeLists.txt in a cross build
# (cmake/arm-none-eabi-cm4f.cmake), so its paths are the project's.
set(OO_C_TARGET "mps2_an386" CACHE STRING
    "The image: xmc4500 (the bike's main board) or mps2_an386 (QEMU's stand-in for it)")
set_property(CACHE OO_C_TARGET PROPERTY STRINGS xmc4500 mps2_an386)
if(NOT OO_C_TARGET MATCHES "^(xmc4500|mps2_an386)$")
    message(FATAL_ERROR "OO_C_TARGET is xmc4500 or mps2_an386, not '${OO_C_TARGET}'")
endif()

# Vendor files, each fetched at a pinned version and checked against its hash.
function(oo_fetch_file url sha256 destination)
    file(DOWNLOAD ${url} ${destination} EXPECTED_HASH SHA256=${sha256} STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "Fetching ${url}: ${status}")
    endif()
endfunction()

# ARM's CMSIS core headers, 5.9.0 (Apache-2.0), which every device header builds on.
set(CMSIS_URL https://raw.githubusercontent.com/ARM-software/CMSIS_5/5.9.0)
set(CMSIS_DIR ${CMAKE_BINARY_DIR}/_deps/cmsis)
foreach(file_and_hash IN ITEMS
        core_cm4.h:f5b63d52dd1557b15ca414cb59c264f595a7b5669db2a5878dffe22f4caedc8c
        cmsis_compiler.h:b51963d271c1571ca3463654c76aa7ea50c4eac5d0a2710df10414ac64d9e0f8
        cmsis_gcc.h:43bfd1fe69fbbc2ca70aaf7d43cc3e20f16e0c7f66e1bdef9b51af6dc14b2617
        cmsis_version.h:184c19fd3ee73632edf35a0b4d49cd48be75fbf49e6ccb19d9db05fa83bea4b3
        mpu_armv7.h:29206b52ee02290ed6f5a5415ebd4187de802cf176d9b3cb844390d8e5571372)
    string(REPLACE ":" ";" pair ${file_and_hash})
    list(GET pair 0 file)
    list(GET pair 1 hash)
    oo_fetch_file(${CMSIS_URL}/CMSIS/Core/Include/${file} ${hash} ${CMSIS_DIR}/${file})
endforeach()

# The image's device: its device.h, which FreeRTOSConfig.h includes, and the CMSIS headers under
# it, as system headers, which the library's warnings don't apply to.
add_library(device INTERFACE)
target_include_directories(device INTERFACE target/${OO_C_TARGET}/include)
target_include_directories(device SYSTEM INTERFACE ${CMSIS_DIR})
if(OO_C_TARGET STREQUAL "xmc4500")
    # Infineon's CMSIS device files for the XMC4500, from mtb-xmclib-cat3 4.7.0 (Boost Software
    # License 1.0): its device header, and the startup and clock setup its image links.
    set(XMC_URL https://raw.githubusercontent.com/Infineon/mtb-xmclib-cat3/c24888699c6c5cfd6e5475be90d9703e43540d04/CMSIS/Infineon/COMPONENT_XMC4500)
    set(XMC_DIR ${CMAKE_BINARY_DIR}/_deps/xmc4500)
    foreach(file_and_hash IN ITEMS
            Include/XMC4500.h:462574fa3a955da53cab81c0ea7ff1545925afc87918d21f2ab66fdd2bc3ff8e
            Include/system_XMC4500.h:87adfca40c9503040b1ba8a46c33bfb3e572ad21b4c9da6d4bdf0a38f99d2709
            Source/system_XMC4500.c:5b004fcf2a2587061983bd6481cd678ea92a27eff38b3b32035e9a83d8d75294
            Source/TOOLCHAIN_GCC_ARM/startup_XMC4500.S:b80aca1fd3bf6a9bafa5dcab5c75a7f39fa3538b2a392c23680a69c4300e4df5)
        string(REPLACE ":" ";" pair ${file_and_hash})
        list(GET pair 0 file)
        list(GET pair 1 hash)
        get_filename_component(name ${file} NAME)
        oo_fetch_file(${XMC_URL}/${file} ${hash} ${XMC_DIR}/${name})
    endforeach()
    target_include_directories(device SYSTEM INTERFACE ${XMC_DIR})
endif()

set(OO_C_PORT cm4f)
include(rtos/rtos.cmake)

include(target/${OO_C_TARGET}/image.cmake)
