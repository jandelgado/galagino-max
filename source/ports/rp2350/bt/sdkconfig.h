#ifndef GALAGINO_BP32_SDKCONFIG_H
#define GALAGINO_BP32_SDKCONFIG_H

// Bluepad32 "menuconfig" for galagino: one pad, custom platform
// (hal/rp2350/gamepad_bt.cpp), errors only in the log.
#define CONFIG_BLUEPAD32_MAX_DEVICES 1
#define CONFIG_BLUEPAD32_MAX_ALLOWLIST 1
#define CONFIG_BLUEPAD32_GAP_SECURITY 1
#define CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#define CONFIG_TARGET_PICO_W
#define CONFIG_BLUEPAD32_LOG_LEVEL 1

#endif // GALAGINO_BP32_SDKCONFIG_H
