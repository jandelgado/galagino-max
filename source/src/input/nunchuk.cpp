#include "nunchuk.h"

#ifdef NUNCHUCK_INPUT
#include <stdio.h>
#include "../hal/hal.h"
#include "buttons.h"

static constexpr int PORT = 0;
static constexpr uint8_t ADDR = 0x52;
static constexpr uint32_t BUS_HZ = 400000;
static constexpr uint32_t POLL_MS = 100;
static constexpr uint8_t STICK_CENTER = 127;
static constexpr uint32_t REPORT_BYTES = 6;
static constexpr uint8_t BUTTON_Z_BIT = 0x01;  // report byte 5, active low
static constexpr uint8_t BUTTON_C_BIT = 0x02;

// Asks for the next report. The Nunchuk needs >= 200 us to sample, the
// next poll comes POLL_MS later.
static void request() {
  static const uint8_t START = 0x00;
  hal::I2c::write(PORT, ADDR, &START, 1);
}

void Nunchuk::setup() {
  hal::I2c::init(PORT, NUNCHUCK_SDA, NUNCHUCK_SCL, BUS_HZ);

  // F0 55, FB 00: unencrypted reports (works on clones too)
  static const uint8_t INIT1[2] = {0xf0, 0x55};
  static const uint8_t INIT2[2] = {0xfb, 0x00};
  if (!hal::I2c::write(PORT, ADDR, INIT1, 2)) {
    printf("Nunchuk not detected!\n");
  }
  hal::sleepMs(10);
  hal::I2c::write(PORT, ADDR, INIT2, 2);
  hal::sleepMs(10);
  request();

  lastPoll = hal::millis();
  enable();
}

void Nunchuk::enable() {
  enabled = true;
}

void Nunchuk::disable() {
  enabled = false;
}

bool Nunchuk::connected() {
  // a probe may read a byte of the pending report (RP2350): ask again
  const bool present = hal::I2c::probe(PORT, ADDR);
  request();
  return present;
}

uint16_t Nunchuk::getInput() {
  const uint32_t now = hal::millis();
  if (!enabled || now - lastPoll < POLL_MS) {
    return lastValue;
  }
  lastPoll = now;

  uint8_t report[REPORT_BYTES];
  const bool ok = hal::I2c::read(PORT, ADDR, report, REPORT_BYTES) == REPORT_BYTES;
  request();
  if (!ok) {
    printf("Nunchuk disconnected!\n");
    return 0;
  }

  // report: joy X, joy Y (0..255, ~127 centered), accel, byte 5 buttons
  const int joyX = report[0];
  const int joyY = report[1];
  lastValue = ((joyX < STICK_CENTER - NUNCHUCK_MOVE_THRESHOLD) ? BUTTON_LEFT : 0) |
              ((joyX > STICK_CENTER + NUNCHUCK_MOVE_THRESHOLD) ? BUTTON_RIGHT : 0) |
              ((joyY > STICK_CENTER + NUNCHUCK_MOVE_THRESHOLD) ? BUTTON_UP : 0) |
              ((joyY < STICK_CENTER - NUNCHUCK_MOVE_THRESHOLD) ? BUTTON_DOWN : 0) |
              ((report[5] & BUTTON_Z_BIT) ? 0 : BUTTON_FIRE) |
              ((report[5] & BUTTON_C_BIT) ? 0 : BUTTON_EXTRA);
  return lastValue;
}

#endif
