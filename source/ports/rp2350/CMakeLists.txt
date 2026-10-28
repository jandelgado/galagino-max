# galagino on the RP2350 (Raspberry Pi Pico 2). Configure with build.sh: it
# generates the defines and source list from platformio.ini first.
cmake_minimum_required(VERSION 3.13)

# board profile (src/boards/<name>.h) and its pico-sdk board
set(GALAGINO_BOARD pico2 CACHE STRING "pico2, pico2w_dvi or feather_rp2350")
if(GALAGINO_BOARD STREQUAL "pico2")
  set(PICO_BOARD pico2)
elseif(GALAGINO_BOARD STREQUAL "pico2w_dvi")
  set(PICO_BOARD pico2_w)
elseif(GALAGINO_BOARD STREQUAL "feather_rp2350")
  set(PICO_BOARD adafruit_feather_rp2350)
else()
  message(FATAL_ERROR "unknown GALAGINO_BOARD ${GALAGINO_BOARD}")
endif()
string(TOUPPER "BOARD_${GALAGINO_BOARD}" GALAGINO_BOARD_DEFINE)
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
target_compile_definitions(galagino PRIVATE ${GALAGINO_DEFINES} ${GALAGINO_BOARD_DEFINE}
  PICO_FLASH_SIZE_BYTES=4194304 PICO_STACK_SIZE=0x1000 PICO_PANIC_FUNCTION=galaginoPanic)
target_compile_options(galagino PRIVATE -O2 -w)
target_link_libraries(galagino PRIVATE pico_stdlib pico_rand pico_multicore hardware_i2c
  hardware_dma hardware_watchdog hardware_vreg FreeRTOS-Kernel FreeRTOS-Kernel-Heap4)

pico_enable_stdio_usb(galagino 1)
pico_enable_stdio_uart(galagino 0)
pico_add_extra_outputs(galagino)
