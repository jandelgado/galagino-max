# DVI test image (dvi_test.c). Build: ./build.sh [board], board as in the
# galagino build (pico2w_dvi, feather_rp2350).
cmake_minimum_required(VERSION 3.13)

set(GALAGINO_BOARD pico2w_dvi CACHE STRING "pico2w_dvi or feather_rp2350")
if(GALAGINO_BOARD STREQUAL "pico2w_dvi")
  set(PICO_BOARD pico2_w)
elseif(GALAGINO_BOARD STREQUAL "feather_rp2350")
  set(PICO_BOARD adafruit_feather_rp2350)
else()
  message(FATAL_ERROR "unknown GALAGINO_BOARD ${GALAGINO_BOARD}")
endif()
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "")

include(${CMAKE_CURRENT_LIST_DIR}/../pico_sdk_import.cmake)

project(dvi_test C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(dvi_test dvi_test.c)
# the HSTX pins come from the galagino board profile
target_compile_definitions(dvi_test PRIVATE
  BOARD_PROFILE="${CMAKE_CURRENT_LIST_DIR}/../../../src/boards/${GALAGINO_BOARD}.h")
target_link_libraries(dvi_test PRIVATE pico_stdlib hardware_dma)
pico_enable_stdio_usb(dvi_test 1)
pico_enable_stdio_uart(dvi_test 0)
pico_add_extra_outputs(dvi_test)
# -DDVI_TEST_DEFAULT_CLOCKS=ON: SDK default clocks (150 MHz), for diagnosis
if(DVI_TEST_DEFAULT_CLOCKS)
  target_compile_definitions(dvi_test PRIVATE DEFAULT_CLOCKS)
endif()
