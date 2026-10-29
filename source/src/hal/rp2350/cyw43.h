#ifndef HAL_RP2350_CYW43_H
#define HAL_RP2350_CYW43_H

struct async_context;

namespace hal {

// The CYW43 of the Pico 2 W, shared by the Bluetooth pad and WiFi: one
// FreeRTOS async_context task on the video core runs the driver, BTstack
// and lwIP (its lock is lwIP's core lock). The first call starts it;
// nullptr if that failed.
async_context *cyw43Begin();

// Bluetooth inquiry scans (looking for a pad) take the shared radio: WiFi
// finds no AP while they run. WiFi pauses them from begin() to down().
// Defined by gamepad_bt.cpp, a no-op without the Bluetooth pad.
enum class BtScan { PAUSED, RESUMED };
void btScan(BtScan scan);

} // namespace hal

#endif // HAL_RP2350_CYW43_H
