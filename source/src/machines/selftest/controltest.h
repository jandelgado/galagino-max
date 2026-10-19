#ifndef CONTROLTEST_H
#define CONTROLTEST_H

#include "../../config.h"

#ifdef BOOT_SELFTEST
#include <stdint.h>
#include "../../emulation/input.h"

// controller test page of the self-test: one row per physical input.
// "--" never pressed, "ON" held now, "OK" pressed before. A switch stuck
// at ON or never reaching OK is broken
class ControlTest {
public:
  // EXIT_NONE: single machine builds, input never resets, so no exit hint
  enum Exit : uint8_t { EXIT_NONE, EXIT_RESET };

  ControlTest(Input *input, Exit exit);

  // once per frame with the current buttons_get() keys
  void frame(unsigned int keys, uint32_t ms);

  // first held row, -1 if none. picks the tone pitch
  int8_t heldRow() const;

  void draw(uint16_t *frame_buffer, uint16_t width, uint8_t row) const;

private:
  struct Row {
    const char *label;
    uint16_t mask;  // 0: input not present in this build
  };
  static const Row ROWS[];
  static const uint8_t ROW_COUNT;

  Input *input;
  Exit exit;
  unsigned int held = 0;
  unsigned int seen = 0;
#ifdef NUNCHUCK_INPUT
  bool nunchuck_polled = false;
  bool nunchuck_ok = false;
  uint32_t nunchuck_ms = 0;
#endif
};
#endif

#endif // CONTROLTEST_H
