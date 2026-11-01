// Replaces Bluepad32's include/uni_config.h (force-included, same guard):
// Bluetooth Classic only. The 8BitDo SN30 Pro / Pro 2 are classic HID pads;
// BLE on costs ~85 KB flash and ~5 KB RAM.
#ifndef UNI_CONFIG_H
#define UNI_CONFIG_H

#include "sdkconfig.h"

#define UNI_ENABLE_BREDR 1
#define UNI_ENABLE_BLE 0

#endif // UNI_CONFIG_H
