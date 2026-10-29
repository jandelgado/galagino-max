# Bluepad32 (Apache-2.0) and BTstack over the CYW43 for hal/rp2350/gamepad_bt.cpp.
# Compiled into the galagino target with our configs (this directory);
# Bluepad32's own CMakeLists links the polling CYW43 arch, we run FreeRTOS.
# Bluepad32 4.2.0, pico-sdk 2.2.0 (Bluepad32 does not build with 2.3.0 yet).

set(BLUEPAD32_PATH $ENV{BLUEPAD32_PATH})
if(NOT BLUEPAD32_PATH OR NOT EXISTS ${BLUEPAD32_PATH}/src/components/bluepad32)
  message(FATAL_ERROR "BLUEPAD32_PATH: Bluepad32 4.2.0 checkout (see build.sh)")
endif()
set(BP32 ${BLUEPAD32_PATH}/src/components/bluepad32)

# their source list for the Pico W (components/bluepad32/CMakeLists.txt)
set(BP32_SOURCES
  bt/uni_bt.c bt/uni_bt_allowlist.c bt/uni_bt_conn.c bt/uni_bt_hci_cmd.c bt/uni_bt_le.c
  bt/uni_bt_service.c bt/uni_bt_setup.c bt/uni_bt_bredr.c bt/uni_bt_sdp.c
  controller/uni_balance_board.c controller/uni_controller.c controller/uni_controller_type.c
  controller/uni_gamepad.c controller/uni_keyboard.c controller/uni_mouse.c
  parser/uni_hid_parser.c parser/uni_hid_parser_8bitdo.c parser/uni_hid_parser_android.c
  parser/uni_hid_parser_atari.c parser/uni_hid_parser_ds3.c parser/uni_hid_parser_ds4.c
  parser/uni_hid_parser_ds5.c parser/uni_hid_parser_generic.c parser/uni_hid_parser_icade.c
  parser/uni_hid_parser_keyboard.c parser/uni_hid_parser_mouse.c parser/uni_hid_parser_nimbus.c
  parser/uni_hid_parser_ouya.c parser/uni_hid_parser_psmove.c
  parser/uni_hid_parser_smarttvremote.c parser/uni_hid_parser_stadia.c
  parser/uni_hid_parser_steam.c parser/uni_hid_parser_switch.c parser/uni_hid_parser_wii.c
  parser/uni_hid_parser_xboxone.c
  platform/uni_platform.c uni_circular_buffer.c uni_hid_device.c uni_init.c uni_joystick.c
  uni_log.c uni_property.c uni_utils.c uni_version.c uni_virtual_device.c
  arch/uni_console_pico.c arch/uni_system_pico.c arch/uni_log_pico.c arch/uni_property_pico.c)
list(TRANSFORM BP32_SOURCES PREPEND ${BP32}/)

set(BP32_BACKEND ${SRC}/hal/rp2350/gamepad_bt.cpp)
target_sources(galagino PRIVATE ${BP32_SOURCES})
# BTstack's sources (pico_btstack_* INTERFACE) need btstack_config.h too
target_include_directories(galagino PRIVATE ${CMAKE_CURRENT_LIST_DIR})
set_source_files_properties(${BP32_SOURCES} ${BP32_BACKEND} PROPERTIES
  INCLUDE_DIRECTORIES ${BP32}/include
  COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/uni_config.h")
target_link_libraries(galagino PRIVATE pico_cyw43_arch_sys_freertos pico_btstack_ble
  pico_btstack_classic pico_btstack_cyw43)
# CYW43 PIO SPI: the stock divider 2 is for 150 MHz; at 252 MHz 3.36 keeps
# the same 75 MHz PIO clock
target_compile_definitions(galagino PRIVATE CYW43_PIO_CLOCK_DIV_INT=3 CYW43_PIO_CLOCK_DIV_FRAC8=92
  CYW43_LWIP=0)  # Bluetooth only, no IP stack
