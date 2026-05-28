# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.15)

include(pico_sdk_import.cmake)
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel)
include(FreeRTOS_Kernel_import.cmake)

set(MODEL_FOLDER ei-model)
set(EI_SDK_FOLDER ${MODEL_FOLDER}/edge-impulse-sdk)
set(DRIVERS Sensors)
set(EI_PLATFORM_FOLDER edge-impulse)
set(FIRMWARE_SDK_FOLDER firmware-sdk)

if(PICO_BOARD STREQUAL "pico")
    set(RP_PROJCT_NAME ei_rp2040_firmware)
else()
    set(RP_PROJCT_NAME ei_rp2350_firmware)
endif()

project(pico_emb C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


pico_sdk_init()

add_definitions(-DEIDSP_QUANTIZE_FILTERBANK=0
                -DFREERTOS_ENABLED=0
               -DARM_MATH_LOOPUNROLL
                                )

# Add subdirectory for main executable
add_subdirectory(main)
