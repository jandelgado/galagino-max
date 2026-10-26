# ESP-IDF only build (PlatformIO framework = espidf, envs idf-*). The Arduino
# envs do not use this file. Everything under src/; the WiFi loader (src/net,
# hal/esp/wifi.cpp) compiles to nothing without ASSETS_REMOTE.
# C sources first: PlatformIO's espidf builder takes C/C++ compile groups in
# order of the first source; a .cpp first hands C-only options
# (-Wno-old-style-declaration) to every C++ file, which -Werror rejects.
file(GLOB_RECURSE c_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
file(GLOB_RECURSE cpp_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set(app_sources ${c_sources} ${cpp_sources})

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        app_update bootloader_support esp_app_format esp_driver_dac esp_driver_gpio
        esp_driver_i2c esp_driver_rmt esp_driver_spi esp_event esp_hw_support esp_netif
        esp_partition esp_rom esp_system esp_timer esp_wifi heap lwip spi_flash)

# IDF builds every component with -fno-jump-tables (jump tables in flash would
# break code that runs with the flash cache off). The CPU cores' opcode
# switches then compile to compare chains and the emulation runs 25 to 40 %
# slower: give this component jump tables back, as the Arduino build has.
target_compile_options(${COMPONENT_LIB} PRIVATE -fjump-tables -ftree-switch-conversion -flto=auto)
