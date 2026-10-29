#ifdef HAL_RP2350
#include "../../config.h"  // the board profile selects the backend
#ifdef HAL_GAMEPAD_BT
#include "../gamepad.h"
#include "cyw43.h"
#include "rtos.h"
#include <stdio.h>
#include <pico/async_context.h>
extern "C" {
#include <uni.h>
}

// Bluepad32 runs as a "custom platform": its callbacks below come from the
// CYW43 async_context task (cyw43.h, BTstack's run loop) on the video core. They
// publish the pad as a GamepadState; read() copies it on any core.
//
//   video core                                   any core
//   async_context task: CYW43 IRQ work, BTstack,
//   Bluepad32 parsers -> onControllerData()
//                           |  CriticalSection
//                           v
//                         state  ------------->  Gamepad::read()

namespace hal {
namespace {

GamepadState state;
// pad connected; WiFi holds the scans off (btScan)
volatile bool padReady;
volatile bool scanPaused;

int16_t clampAxis(int32_t v) {
  if (v < GamepadState::STICK_MIN) {
    return GamepadState::STICK_MIN;
  }
  if (v > GamepadState::STICK_MAX) {
    return GamepadState::STICK_MAX;
  }
  return (int16_t)v;
}

// Bluepad32's bits to GamepadState's
struct BitMap {
  uint16_t from;
  uint16_t to;
};

const BitMap DPAD_MAP[] = {
  {DPAD_UP, GamepadState::UP}, {DPAD_DOWN, GamepadState::DOWN},
  {DPAD_LEFT, GamepadState::LEFT}, {DPAD_RIGHT, GamepadState::RIGHT},
};
const BitMap BUTTON_MAP[] = {
  {BUTTON_A, GamepadState::A}, {BUTTON_B, GamepadState::B},
  {BUTTON_X, GamepadState::X}, {BUTTON_Y, GamepadState::Y},
  {BUTTON_SHOULDER_L, GamepadState::L1}, {BUTTON_SHOULDER_R, GamepadState::R1},
  {BUTTON_TRIGGER_L, GamepadState::L2}, {BUTTON_TRIGGER_R, GamepadState::R2},
};
const BitMap MISC_MAP[] = {
  {MISC_BUTTON_SELECT, GamepadState::SELECT}, {MISC_BUTTON_START, GamepadState::START},
  {MISC_BUTTON_SYSTEM, GamepadState::SYSTEM},
};

template <size_t N>
uint16_t mapBits(uint32_t bits, const BitMap (&map)[N]) {
  uint16_t out = 0;
  for (const BitMap &m : map) {
    if (bits & m.from) {
      out |= m.to;
    }
  }
  return out;
}

void publish(const GamepadState &s) {
  CriticalSection lock;
  state = s;
}

// a pad that vanishes (battery, range) drops after 2 s, not BTstack's
// default 20 s with its last buttons held; unit 0.625 ms
const uint16_t LINK_SUPERVISION_SLOTS = 3200;

void onInit(int, const char **) {
  gap_set_link_supervision_timeout(LINK_SUPERVISION_SLOTS);
}

void onInitComplete() {
  // stored link keys stay: a paired pad reconnects without pairing
  uni_bt_start_scanning_and_autoconnect_unsafe();
  printf("Bluetooth: ready, pairing open\n");
}

uni_error_t onDiscovered(bd_addr_t, const char *, uint16_t cod, uint8_t) {
  // ignore keyboards
  if ((cod & UNI_BT_COD_MINOR_MASK & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD) {
    return UNI_ERROR_IGNORE_DEVICE;
  }
  return UNI_ERROR_SUCCESS;
}

void onConnected(uni_hid_device_t *) {}

void onDisconnected(uni_hid_device_t *) {
  publish(GamepadState());
  padReady = false;
  if (!scanPaused) {
    uni_bt_start_scanning_and_autoconnect_unsafe();
  }
  printf("Bluetooth: pad disconnected\n");
}

uni_error_t onReady(uni_hid_device_t *d) {
  // inquiry scans share the radio with the pad's reports
  uni_bt_stop_scanning_unsafe();
  padReady = true;
  printf("Bluetooth: pad %s ready\n", d->name);
  return UNI_ERROR_SUCCESS;
}

void onControllerData(uni_hid_device_t *, uni_controller_t *ctl) {
  if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) {
    return;
  }

  const uni_gamepad_t &gp = ctl->gamepad;
  GamepadState s;
  s.connected = true;
  s.dpad = (uint8_t)mapBits(gp.dpad, DPAD_MAP);
  s.x = clampAxis(gp.axis_x);
  s.y = clampAxis(gp.axis_y);
  s.buttons = mapBits(gp.buttons, BUTTON_MAP) | mapBits(gp.misc_buttons, MISC_MAP);
  publish(s);
}

const uni_property_t *getProperty(uni_property_idx_t) { return nullptr; }

void onOobEvent(uni_platform_oob_event_t, void *) {}

uni_platform platform = {
  .name = "galagino",
  .init = onInit,
  .on_init_complete = onInitComplete,
  .on_device_discovered = onDiscovered,
  .on_device_connected = onConnected,
  .on_device_disconnected = onDisconnected,
  .on_device_ready = onReady,
  .on_gamepad_data = nullptr,
  .on_controller_data = onControllerData,
  .get_property = getProperty,
  .on_oob_event = onOobEvent,
  .device_dump = nullptr,
  .register_console_cmds = nullptr,
};

} // namespace

void Gamepad::begin() {
  // the CYW43 and BTstack work runs in its async_context task (cyw43.h)
  async_context *context = cyw43Begin();
  if (!context) {
    printf("Bluetooth: no CYW43\n");
    return;
  }

  // BTstack is not thread safe: set up under its run loop's lock
  async_context_acquire_lock_blocking(context);
  uni_platform_set_custom(&platform);
  uni_init(0, nullptr);
  async_context_release_lock(context);
}

void btScan(BtScan scan) {
  scanPaused = scan == BtScan::PAUSED;
  if (scanPaused) {
    uni_bt_stop_scanning_safe();
  } else if (!padReady) {
    uni_bt_start_scanning_and_autoconnect_safe();
  }
}

void Gamepad::read(GamepadState &out) {
  CriticalSection lock;
  out = state;
}

} // namespace hal
#endif // HAL_GAMEPAD_BT
#endif // HAL_RP2350
