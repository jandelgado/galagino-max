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
  message(FATAL_ERROR "pass -DGALAGINO_CMAKE=<file from tools/config_to_cmake.py> (see build.sh)")
endif()
include(${GALAGINO_CMAKE})

set(SRC ${CMAKE_CURRENT_LIST_DIR}/../../src)
set(SOURCES)
foreach(f ${GALAGINO_SOURCES})
  list(APPEND SOURCES ${SRC}/${f})
endforeach()

set(FLASH_BYTES 4194304)
add_executable(galagino ${SOURCES})
target_include_directories(galagino PRIVATE ${SRC} ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(galagino PRIVATE ${GALAGINO_DEFINES} ${GALAGINO_BOARD_DEFINE}
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
# assets in the flash between the image and FLASH_END. Game functions and the
# 6502 core stay in flash, as on the ESP32: the code of every catalog game is
# in the image, its RAM functions would not fit.
list(FIND GALAGINO_DEFINES ASSETS_REMOTE REMOTE_AT)
if(NOT REMOTE_AT EQUAL -1)
  if(NOT GALAGINO_BOARD STREQUAL "pico2w_dvi")
    message(FATAL_ERROR "MODE=remote needs the CYW43 WiFi: BOARD=pico2w_dvi")
  endif()
  target_compile_definitions(galagino PRIVATE GAME_NO_IRAM M6502_NO_IRAM)
  target_include_directories(galagino PRIVATE ${CMAKE_BINARY_DIR})  # config_wifi.h (build.sh)
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
# (machine object, strip buffers, stdio, input). Reserve not sized yet:
# measure on device once the input backends are in.
set(HEAP_RESERVE 0)
string(REGEX MATCH "ARENA_A_SIZE=([0-9]+)" _ "${GALAGINO_DEFINES}")
set(ARENA_A ${CMAKE_MATCH_1})
string(REGEX MATCH "ARENA_B_SIZE=([0-9]+)" _ "${GALAGINO_DEFINES}")
math(EXPR ARENA_BYTES "${ARENA_A} + ${CMAKE_MATCH_1}")
target_link_options(galagino PRIVATE -Wl,--defsym=GALAGINO_ARENA_BYTES=${ARENA_BYTES}
  -Wl,--defsym=GALAGINO_HEAP_RESERVE=${HEAP_RESERVE} -Wl,--defsym=GALAGINO_FLASH_END=${FLASH_END}
  ${CMAKE_CURRENT_LIST_DIR}/ram_check.ld)
pico_enable_stdio_usb(galagino 1)
pico_enable_stdio_uart(galagino 0)
pico_add_extra_outputs(galagino)
