# galagino on the RP2350 (Raspberry Pi Pico 2). Configure with build.sh: it
# generates the env's board, defines and source list from firmware.toml first.
cmake_minimum_required(VERSION 3.13)

if(NOT GALAGINO_CMAKE)
  message(FATAL_ERROR "pass -DGALAGINO_CMAKE=<file from tools/config_to_cmake.py> (see build.sh)")
endif()
include(${GALAGINO_CMAKE})
if(NOT "HAL_RP2350" IN_LIST GALAGINO_DEFINES)
  message(FATAL_ERROR "not an rp2350 env of firmware.toml [envs]")
endif()

# the board profile's pico-sdk board
if(GALAGINO_BOARD STREQUAL "pico2")
  set(PICO_BOARD pico2)
elseif(GALAGINO_BOARD STREQUAL "pico2w_dvi")
  set(PICO_BOARD pico2_w)
elseif(GALAGINO_BOARD STREQUAL "feather_rp2350")
  set(PICO_BOARD adafruit_feather_rp2350)
else()
  message(FATAL_ERROR "unknown GALAGINO_BOARD ${GALAGINO_BOARD}")
endif()
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "")
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} CACHE STRING "")

include(pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)

project(galagino_rp2350 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(SRC ${CMAKE_CURRENT_LIST_DIR}/../../src)
set(SOURCES)
foreach(f ${GALAGINO_SOURCES})
  list(APPEND SOURCES ${SRC}/${f})
endforeach()

set(FLASH_BYTES 4194304)
add_executable(galagino ${SOURCES})
target_include_directories(galagino PRIVATE ${SRC} ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(galagino PRIVATE ${GALAGINO_DEFINES}
  PICO_FLASH_SIZE_BYTES=${FLASH_BYTES} PICO_STACK_SIZE=0x1000 PICO_PANIC_FUNCTION=galaginoPanic)
target_compile_options(galagino PRIVATE -O2 -w)
target_link_libraries(galagino PRIVATE pico_stdlib pico_rand pico_multicore hardware_i2c
  hardware_dma hardware_watchdog hardware_vreg FreeRTOS-Kernel FreeRTOS-Kernel-Heap4)

# Bluetooth pad on the Pico 2 W (the profile's default input; with a wired
# source the linker drops the unused BT code); its link keys
# live in pico_btstack's flash bank: 8 KB plus a spare 4 KB sector at the end
# on the RP2350 (PICO_FLASH_BANK_STORAGE_OFFSET)
set(BT_BANK_BYTES 12288)
math(EXPR FLASH_END "0x10000000 + ${FLASH_BYTES}" OUTPUT_FORMAT HEXADECIMAL)
if(GALAGINO_BOARD STREQUAL "pico2w_dvi")
  include(bt/bluepad32.cmake)
  math(EXPR FLASH_END "0x10000000 + ${FLASH_BYTES} - ${BT_BANK_BYTES}" OUTPUT_FORMAT HEXADECIMAL)
endif()

# Remote mode (ASSETS_REMOTE): WiFi over the CYW43 with lwIP, downloaded
# assets in the flash between the image and FLASH_END
if("ASSETS_REMOTE" IN_LIST GALAGINO_DEFINES)
  if(NOT GALAGINO_BOARD STREQUAL "pico2w_dvi")
    message(FATAL_ERROR "remote mode needs the CYW43 WiFi: board pico2w_dvi")
  endif()
  target_link_libraries(galagino PRIVATE pico_cyw43_arch_lwip_sys_freertos hardware_flash)
  target_compile_definitions(galagino PRIVATE CYW43_LWIP=1)
elseif(GALAGINO_BOARD STREQUAL "pico2w_dvi")
  target_link_libraries(galagino PRIVATE pico_cyw43_arch_sys_freertos)
  target_compile_definitions(galagino PRIVATE CYW43_LWIP=0)  # Bluetooth only, no IP stack
endif()
if(GALAGINO_BOARD STREQUAL "pico2w_dvi")
  # CYW43 PIO SPI: the stock divider 2 is for 150 MHz; at 252 MHz 3.36 keeps
  # the same 75 MHz PIO clock
  target_compile_definitions(galagino PRIVATE CYW43_PIO_CLOCK_DIV_INT=3 CYW43_PIO_CLOCK_DIV_FRAC8=92)
endif()

# heap check at link time: Arena blocks plus the other heap users
# (machine object, strip buffers, stdio, input). Pico 2 W DVI: Bluetooth
# takes ~23 KB, the largest machine object is ~12 KB (System 1); 17 KB
# panicked at boot, 29 KB at game start. Other boards not measured yet.
set(HEAP_RESERVE 0)
if(GALAGINO_BOARD STREQUAL "pico2w_dvi")
  set(HEAP_RESERVE 40960)
endif()
string(REGEX MATCH "ARENA_A_SIZE=([0-9]+)" _ "${GALAGINO_DEFINES}")
set(ARENA_A ${CMAKE_MATCH_1})
string(REGEX MATCH "ARENA_B_SIZE=([0-9]+)" _ "${GALAGINO_DEFINES}")
math(EXPR ARENA_BYTES "${ARENA_A} + ${CMAKE_MATCH_1}")
target_link_options(galagino PRIVATE -Wl,--defsym=GALAGINO_ARENA_BYTES=${ARENA_BYTES}
  -Wl,--defsym=GALAGINO_HEAP_RESERVE=${HEAP_RESERVE} -Wl,--defsym=GALAGINO_FLASH_END=${FLASH_END}
  ${CMAKE_CURRENT_LIST_DIR}/ram_check.ld)
pico_enable_stdio_usb(galagino 1)
pico_enable_stdio_uart(galagino 0)
# image <name>_<version>.uf2, name and version for picotool info, as on the ESP32
set_target_properties(galagino PROPERTIES OUTPUT_NAME ${GALAGINO_PROG_NAME}_${GALAGINO_PROG_VERSION})
pico_set_program_name(galagino ${GALAGINO_PROG_NAME})
pico_set_program_version(galagino ${GALAGINO_PROG_VERSION})
target_compile_definitions(galagino PRIVATE PROG_NAME="${GALAGINO_PROG_NAME}"
  PROG_VERSION="${GALAGINO_PROG_VERSION}")
pico_add_extra_outputs(galagino)
