#ifndef MISSILE_H
#define MISSILE_H

#include "missile_logo.h"
#include "missile_dipswitches.h"
#include "missile_video.h"
#include "../../cpus/m6502/m6502.h"
#include "../machineBase.h"
#include "../../emulation/pokey.h"
#include "../../emulation/seqlock.h"

// Atari Missile Command rev 3 (MAME missile), atari/missile.cpp. One M6502 at
// 10 MHz / 8 (half speed on lines 224..255), one POKEY (ALLPOT reads R8 DIPs).
// 256x231 ROT0 3 bpp bitmap in 16 KB DRAM at 0x0000-0x3fff, also work RAM,
// kept in memory[]. Video writes use MADSEL: the data access of a (zp,X)
// instruction goes to VRAM through vram_mad_w/r. 8 pen palette, no sprites.
//
// Unrotated: native lines 24..255 -> strips 4..32, native x 8..247 -> 240
// columns. Trackball fed from the stick; FIRE fires the base under the cursor.
class missile : public machineBase {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> missile_rom;
public:
  missile();
  uint8_t machineType() override { return MCH_MISSILE; }
  void renderAudio(int16_t *out, int n) override {
    Pokey::renderBank(pokey, soundregs, out, n);
    audioClampAll(out, n, AUDIO_MAX);
  }
  void reset() override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  const int renderWidth() override { return missile_video::OUT_WIDTH; }
  const int renderBuffer() override { return missile_video::OUT_WIDTH * 2 * 8; }
  static const Logo &logo() { return missile_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  static uint8_t main_fetch(m6502_t *, uint16_t);
  uint8_t read(uint16_t a);
  void write(uint16_t a, uint8_t v);
  bool madsel_access();
  void run_until(uint32_t cycle);
  uint32_t cpu_clock() const;
  bool pokey_running() const;
  uint8_t pokey_read(uint8_t reg);
  void pokey_write(uint8_t reg, uint8_t v);
  uint8_t in0() const;
  uint8_t in1() const;
  void update_trackball();

  static const int LINES = 256;
  static const int LINE_STEP = 8;
  static const int IRQ_LINES = 64;           // IRQ at V = 0, 64, 128, 192
  static const int IRQ_LENGTH = 32;          // released 32 lines later
  static const int SLOW_LINE = 224;          // half CPU speed from here
  static const uint32_t CYCLES_FAST = 80;    // per line
  static const uint32_t CYCLES_SLOW = 40;
  static const uint32_t CYCLES_PER_FRAME =
      SLOW_LINE * CYCLES_FAST + (LINES - SLOW_LINE) * CYCLES_SLOW;
  static const uint8_t MADSEL_ACCESSES = 4;  // operand, pointer lo, pointer hi, data
  // cursor pixel x in work RAM (found with MAME: equals the drawn cursor x)
  static const uint16_t CURSOR_X = 0x00a5;

  m6502_t m_cpu;
  const unsigned char *rom = nullptr;
  const unsigned char *wprom = nullptr;
  uint8_t ctrld = 0;
  uint8_t madsel = 0;               // bus accesses left until the MADSEL one
  uint8_t track_x = 0, track_y = 0; // 4-bit trackball counters
  uint8_t track_held = 0;           // frames the stick is held
  uint16_t pal[8] = {};
  uint32_t frame_cycles = 0;        // cycles run in the current frame
  uint32_t total_cycles = 0;        // cycles of all finished frames (POKEY clock = CPU clock)
  uint32_t random_base = 0;         // cpu_clock() when SKCTL left reset
  uint8_t allpot = 0;               // ALLPOT latch, read back while in reset
  bool in_vblank = false;
  uint8_t led_frame = 0;            // gameLeds() animation counter

  // palette as of vblank end (line 24), see seqlock.h. VRAM is 16 KB and stays live.
  struct VideoState {
    uint16_t pal[8];
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  uint32_t quad[256];               // VRAM byte -> 4 pens (bits 2-1), one per byte

private:
  Pokey pokey[1] = {Pokey(Pokey::CLOCKS_1250KHZ)};
};

#endif
