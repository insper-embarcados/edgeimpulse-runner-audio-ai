# CMakeLists.txt for main executable target

add_executable(pico_emb main.cpp)

# Place output files in the build root directory
set_target_properties(pico_emb PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Enable stdio USB and UART
pico_enable_stdio_usb(pico_emb 0)
pico_enable_stdio_uart(pico_emb 1)

# Generate PIO headers
include(${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/cmake/utils.cmake)
pico_generate_pio_header(pico_emb ${CMAKE_CURRENT_SOURCE_DIR}/../${DRIVERS}/PDM/src/pdm.pio)

# Link libraries
target_link_libraries(pico_emb
                      pico_async_context_freertos
                      FreeRTOS-Kernel-Heap4
                      pico_stdlib
                      pico_unique_id
                      hardware_gpio
                      hardware_adc
                      hardware_i2c
                      hardware_irq
                      hardware_flash
                      hardware_pio
                      hardware_dma)

if(PICO_CYW43_SUPPORTED)
    target_link_libraries(pico_emb pico_cyw43_arch_none)
endif()

# Compile definitions
target_compile_definitions(pico_emb PRIVATE
    configNUMBER_OF_CORES=2
)

# Include directories (all paths relative to root)
target_include_directories(pico_emb PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../FreeRTOS-Kernel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../${MODEL_FOLDER}
    ${CMAKE_CURRENT_SOURCE_DIR}/../${MODEL_FOLDER}/classifer
    ${CMAKE_CURRENT_SOURCE_DIR}/../${MODEL_FOLDER}/tflite-model
    ${CMAKE_CURRENT_SOURCE_DIR}/../${MODEL_FOLDER}/model-parameters
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/third_party/ruy
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/third_party/gemmlowp
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/third_party/flatbuffers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/tensorflow
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/dsp
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/classifier
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/anomaly
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/CMSIS/NN/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/CMSIS/DSP/PrivateInclude
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/CMSIS/DSP/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/CMSIS/Core/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_SDK_FOLDER}/porting
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_PLATFORM_FOLDER}
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_PLATFORM_FOLDER}/ingestion-sdk-c
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_PLATFORM_FOLDER}/ingestion-sdk-platform/raspberry-rp2xxx
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_PLATFORM_FOLDER}/ingestion-sdk-platform/sensors
    ${CMAKE_CURRENT_SOURCE_DIR}/../${EI_PLATFORM_FOLDER}/inference
    ${CMAKE_CURRENT_SOURCE_DIR}/../${FIRMWARE_SDK_FOLDER}/QCBOR/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../${FIRMWARE_SDK_FOLDER}
    ${CMAKE_CURRENT_SOURCE_DIR}/../${FIRMWARE_SDK_FOLDER}/at-server
    ${CMAKE_CURRENT_SOURCE_DIR}/../${DRIVERS}/Arduino/core/Wire/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../${DRIVERS}/Arduino/libs/Seeed_Arduino_UltrasonicRanger
    ${CMAKE_CURRENT_SOURCE_DIR}/../${DRIVERS}/Arduino/libs/Arduino_LSM6DSOX/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../${DRIVERS}/DHT11
    ${CMAKE_CURRENT_SOURCE_DIR}/../${DRIVERS}/ADXL345
    ${CMAKE_CURRENT_SOURCE_DIR}/../${DRIVERS}/PDM/src/include
)

# Add source files
RECURSIVE_FIND_FILE(SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../${MODEL_FOLDER}/edge-impulse-sdk" "*.cpp")
RECURSIVE_FIND_FILE(CC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../${MODEL_FOLDER}/edge-impulse-sdk" "*.cc")
RECURSIVE_FIND_FILE(S_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../${MODEL_FOLDER}/edge-impulse-sdk" "*.s")
RECURSIVE_FIND_FILE(C_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../${MODEL_FOLDER}/edge-impulse-sdk" "*.c")
RECURSIVE_FIND_FILE(MODEL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../${MODEL_FOLDER}/tflite-model" "*.cpp")
RECURSIVE_FIND_FILE(PORTING_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../${MODEL_FOLDER}/edge-impulse-sdk/porting/raspberry" "*.cpp")

RECURSIVE_FIND_FILE(PLATFORM_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../edge-impulse/ingestion-sdk-platform/raspberry-rp2xxx" "*.cpp")
RECURSIVE_FIND_FILE(SENSORS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../edge-impulse/ingestion-sdk-platform/sensors" "*.cpp")
RECURSIVE_FIND_FILE(INGESTION_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../edge-impulse/ingestion-sdk-c/" "*.cpp")
RECURSIVE_FIND_FILE(INFERENCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../edge-impulse/inference" "*.cpp")
RECURSIVE_FIND_FILE(MBEDTLS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../edge-impulse/mbedtls_hmac_sha256_sw/mbedtls/src" "*.c")
RECURSIVE_FIND_FILE(FIRMWARE_SDK_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../firmware-sdk" "*.cpp")
RECURSIVE_FIND_FILE(QCBOR_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../firmware-sdk/QCBOR/src" "*.c")
RECURSIVE_FIND_FILE(DRIVER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../Sensors" "*.cpp")
RECURSIVE_FIND_FILE(MIC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../Sensors/PDM/src" "*.c")

list(APPEND SOURCE_FILES ${S_FILES})
list(APPEND SOURCE_FILES ${C_FILES})
list(APPEND SOURCE_FILES ${CC_FILES})
list(APPEND SOURCE_FILES ${MODEL_FILES})
list(APPEND SOURCE_FILES ${PLATFORM_FILES})
list(APPEND SOURCE_FILES ${SENSORS_FILES})
list(APPEND SOURCE_FILES ${INGESTION_FILES})
list(APPEND SOURCE_FILES ${INFERENCE_FILES})
list(APPEND SOURCE_FILES ${QCBOR_FILES})
list(APPEND SOURCE_FILES ${MBEDTLS_FILES})
list(APPEND SOURCE_FILES ${PORTING_FILES})
list(APPEND SOURCE_FILES ${FIRMWARE_SDK_FILES})
list(APPEND SOURCE_FILES ${DRIVER_FILES})
list(APPEND SOURCE_FILES ${MIC_FILES})

target_sources(pico_emb PRIVATE ${SOURCE_FILES})

# Generate output files
pico_add_extra_outputs(pico_emb)

# Copy .uf2 file to build root directory
add_custom_command(TARGET pico_emb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/pico_emb.uf2"
            "${CMAKE_BINARY_DIR}/pico_emb.uf2"
    COMMENT "Copying pico_emb.uf2 to build root directory"
)
