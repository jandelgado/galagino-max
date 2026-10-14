#ifndef SELFTEST_H
#define SELFTEST_H

#include "../../config.h"

#ifdef BOOT_SELFTEST
#include <stdint.h>
#include "../machineBase.h"
#include "../../emulation/input.h"
#include "../../emulation/sid.h"

// Arcade style boot screen, run like any machine: main starts it first and
// switches on once done(). Title, hardware info lines, crosshatch that
// splits into a red/green anaglyph to a beating SID tone. Sequence defined
// by PHASES in selftest.cpp. No CPU: timed by millis().
class SelfTest : public machineBase {
public:
  // runs the checks, games is the number shown
  SelfTest(Input *input, uint8_t games);

  void start() override;
  void prepare_frame() override;
  void render_row(short row) override;
  void renderSid(int16_t *out, int n) override;
  signed char machineType() override { return MCH_SELFTEST; }
#ifdef LED_PIN
  void gameLeds(CRGB *leds) override;
#endif

  // every phase shown
  bool done() const;

private:
  static const uint16_t WIDTH = 224;  // machineBase::renderWidth()

  // info lines, top to bottom
  enum Line : uint8_t { LINE_CHIP, LINE_CPU, LINE_RAM, LINE_ROM, LINE_FLASH, LINE_GAMES, LINE_NUNCHUCK, LINE_COUNT };
  static const uint8_t LINE_CHARS = WIDTH / 8 + 1;

  // one strip row of a phase to draw
  struct Draw {
    uint16_t *frame_buffer;
    uint8_t row;
    uint32_t ms;        // time since phase start, keeps running after the phase ends
    uint32_t duration;  // of the phase
  };

  struct Phase {
    uint32_t ms;   // duration
    uint32_t led;  // 0xRRGGBB or LEDS_KEEP
    void (*draw)(const SelfTest &test, const Draw &d);
    void (*tone)(SelfTest &test, uint32_t ms, uint32_t duration);  // once per frame, optional
  };
  static const Phase PHASES[];
  static const uint8_t PHASE_COUNT;

  static void drawTitle(const Draw &d);
  static void drawMax(const Draw &d);
  static void drawText(const Draw &d, Line line, const char *text);
  static void crosshatch(const Draw &d, uint32_t fade);
  static void anaglyph(const Draw &d);

  void setLine(Line line, const char *fmt, ...);
  void drawLine(const Draw &d, Line line) const;
  void drawGames(const Draw &d) const;

  void toneSetup();
  void anaglyphTone(uint32_t ms, uint32_t duration);
  void sidVoice(uint8_t v, uint8_t reg, uint8_t val);
  void sidFreq(uint8_t v, uint16_t freq);

  char lines[LINE_COUNT][LINE_CHARS] = {};
  uint8_t games_total;
  uint32_t start_ms;
  uint32_t frame_ms = 0;
  uint8_t phase = 0;  // index into PHASES, PHASE_COUNT once done
  Sid sid;
};
#endif

#endif // SELFTEST_H
