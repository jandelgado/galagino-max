#ifndef HAL_CONFIG_CHECK_H
#define HAL_CONFIG_CHECK_H

// Compile-time checks of the build combination: platform, board profile,
// video and audio backend. Included at the end of config.h. tools/test_config_check.py
// compiles each rule with a bad combination.

#if defined(HAL_ESP) + defined(HAL_RP2350) + defined(HAL_HOST) > 1
#error "config: define only one of HAL_ESP, HAL_RP2350, HAL_HOST"
#endif

// Arena block sizes come from the env's layout, [arena] in firmware.toml
// (extraScript.py, tools/config_to_cmake.py), the table romconv plans against
#if !defined(ARENA_A_SIZE) || !defined(ARENA_B_SIZE)
#error "config: ARENA_A_SIZE/ARENA_B_SIZE missing: build an env of [envs] in firmware.toml"
#endif

#if defined(BOARD_PICO2) + defined(BOARD_FEATHER_RP2350) + defined(BOARD_PICO2W_DVI) + \
    defined(BOARD_AZDELIVERY) + defined(BOARD_CYDMANUAL) + defined(BOARD_CYD2432S028R) + \
    defined(BOARD_FNK0103F) + defined(BOARD_FNK0103B) + defined(BOARD_FNK0104) > 1
#error "config: define only one BOARD_<NAME>"
#endif

#if defined(BOARD_PICO2) && !defined(HAL_RP2350)
#error "config: BOARD_PICO2 needs HAL_RP2350 (build with ports/rp2350/build.sh)"
#endif
#if (defined(BOARD_FEATHER_RP2350) || defined(BOARD_PICO2W_DVI)) && !defined(HAL_RP2350)
#error "config: RP2350 board profiles need HAL_RP2350 (build with ports/rp2350/build.sh)"
#endif
#if (defined(BOARD_AZDELIVERY) || defined(BOARD_CYDMANUAL) || defined(BOARD_CYD2432S028R) || \
     defined(BOARD_FNK0103F) || defined(BOARD_FNK0103B) || defined(BOARD_FNK0104)) && !defined(HAL_ESP)
#error "config: ESP32 board profiles need HAL_ESP"
#endif

#if defined(HAL_VIDEO_SPI_TFT) + defined(HAL_VIDEO_NULL) + defined(HAL_VIDEO_DVI_HSTX) > 1
#error "config: define only one HAL_VIDEO_* backend (the board profile sets it)"
#endif
#if defined(HAL_VIDEO_DVI_HSTX) && !defined(HAL_RP2350)
#error "config: HAL_VIDEO_DVI_HSTX needs HAL_RP2350 (HSTX is an RP2350 peripheral)"
#endif
#if defined(HAL_VIDEO_SPI_TFT) && (!defined(TFT_CS) || !defined(TFT_DC))
#error "config: HAL_VIDEO_SPI_TFT needs TFT_CS and TFT_DC (board profile or config_local.h)"
#endif

#if defined(HAL_AUDIO_ESP_DAC) + defined(HAL_AUDIO_ES8311) + defined(HAL_AUDIO_DVI) + \
    defined(HAL_AUDIO_NULL) > 1
#error "config: define only one HAL_AUDIO_* backend (the board profile sets it)"
#endif
#if defined(HAL_AUDIO_ESP_DAC) && !defined(HAL_ESP)
#error "config: HAL_AUDIO_ESP_DAC needs HAL_ESP (built-in DAC of the ESP32)"
#endif
#if defined(HAL_AUDIO_ES8311) && !defined(HAL_ESP)
#error "config: HAL_AUDIO_ES8311 needs HAL_ESP (no I2S transport on other platforms yet)"
#endif
#if defined(HAL_AUDIO_ES8311) && (!defined(ES8311_I2C_SDA) || !defined(ES8311_I2C_SCL) || \
    !defined(ES8311_I2C_ADDR) || !defined(ES8311_I2C_CLK) || !defined(ES8311_I2S_MCK) || !defined(ES8311_I2S_BCK) || \
    !defined(ES8311_I2S_WS) || !defined(ES8311_I2S_DOUT))
#error "config: HAL_AUDIO_ES8311 needs ES8311_I2C_SDA/SCL/ADDR/CLK and ES8311_I2S_MCK/BCK/WS/DOUT"
#endif
// Input backends on I2C: Input reads one of them
#if defined(NUNCHUCK_INPUT) && defined(GALAGINO_CONTROLLER)
#error "config: define only one of NUNCHUCK_INPUT, GALAGINO_CONTROLLER"
#endif
#if defined(NUNCHUCK_INPUT) && (!defined(NUNCHUCK_SDA) || !defined(NUNCHUCK_SCL) || \
    !defined(NUNCHUCK_MOVE_THRESHOLD))
#error "config: NUNCHUCK_INPUT needs NUNCHUCK_SDA, NUNCHUCK_SCL and NUNCHUCK_MOVE_THRESHOLD"
#endif

// HDMI audio rides in the DVI stream: one without the other is not a build.
// HAL_AUDIO_NULL: plain DVI, silent, for sinks that reject HDMI data islands.
#if defined(HAL_VIDEO_DVI_HSTX) && !defined(HAL_AUDIO_DVI) && !defined(HAL_AUDIO_NULL)
#error "config: HAL_VIDEO_DVI_HSTX needs HAL_AUDIO_DVI or HAL_AUDIO_NULL (DVI builds play sound over DVI only)"
#endif
#if defined(HAL_VIDEO_DVI_HSTX) && (!defined(HSTX_CK_PIN) || !defined(HSTX_D0_PIN) || \
    !defined(HSTX_D1_PIN) || !defined(HSTX_D2_PIN))
#error "config: HAL_VIDEO_DVI_HSTX needs HSTX_CK_PIN, HSTX_D0_PIN, HSTX_D1_PIN, HSTX_D2_PIN (board profile)"
#endif
#if defined(HAL_VIDEO_DVI_HSTX) && (!defined(DVI_SCALE) || (DVI_SCALE != 1 && DVI_SCALE != 2))
#error "config: DVI_SCALE must be 1 or 2 (1x, or 2x wide with scaled rows)"
#endif
#if defined(HAL_VIDEO_DVI_HSTX) && !defined(ARENA_LAYOUT_RP2350_DVI) && !defined(ARENA_LAYOUT_RP2350_DVI_REMOTE) && \
    !defined(ARENA_LAYOUT_RP2350_DVI_BT)
#error "config: HAL_VIDEO_DVI_HSTX needs the rp2350_dvi Arena layout (the framebuffer takes part of block B)"
#endif
#if defined(HAL_AUDIO_DVI) && !defined(HAL_VIDEO_DVI_HSTX)
#error "config: HAL_AUDIO_DVI needs HAL_VIDEO_DVI_HSTX"
#endif

// Bluetooth pad: radio of the Pico 2 W (host: scripted tests), the only
// input source of the build
#if defined(HAL_GAMEPAD_BT) && (defined(NUNCHUCK_INPUT) || defined(GALAGINO_CONTROLLER))
#error "config: HAL_GAMEPAD_BT is the only input source, drop NUNCHUCK_INPUT/GALAGINO_CONTROLLER"
#endif
#if defined(HAL_GAMEPAD_BT) && !defined(HAL_HOST) && !defined(BOARD_PICO2W_DVI)
#error "config: HAL_GAMEPAD_BT needs a CYW43 board (Pico 2 W)"
#endif

#endif // HAL_CONFIG_CHECK_H
