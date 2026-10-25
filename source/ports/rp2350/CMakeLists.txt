# galagino on the RP2350 (Raspberry Pi Pico 2). Configure with build.sh: it
# generates the defines and source list from platformio.ini first.
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2 CACHE STRING "")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "")
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} CACHE STRING "")

include(pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)

project(galagino_rp2350 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

if(NOT GALAGINO_CMAKE)
  message(FATAL_ERROR "pass -DGALAGINO_CMAKE=<file from tools/pio_to_cmake.py> (see build.sh)")
endif()
include(${GALAGINO_CMAKE})

set(SRC ${CMAKE_CURRENT_LIST_DIR}/../../src)
set(SOURCES)
foreach(f ${GALAGINO_SOURCES})
  list(APPEND SOURCES ${SRC}/${f})
endforeach()

add_executable(galagino ${SOURCES})
target_include_directories(galagino PRIVATE ${SRC} ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(galagino PRIVATE ${GALAGINO_DEFINES} BOARD_PICO2
  PICO_FLASH_SIZE_BYTES=4194304 PICO_STACK_SIZE=0x1000)
target_compile_options(galagino PRIVATE -O2 -w)
target_link_libraries(galagino PRIVATE pico_stdlib pico_rand pico_multicore hardware_i2c
  hardware_watchdog hardware_vreg FreeRTOS-Kernel FreeRTOS-Kernel-Heap4)

pico_enable_stdio_usb(galagino 1)
pico_enable_stdio_uart(galagino 0)
pico_add_extra_outputs(galagino)
