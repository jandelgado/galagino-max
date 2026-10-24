#ifndef ATETRIS_H
#define ATETRIS_H

#include "atetris_logo.h"
#include "atetris_dipswitches.h"
#include "atetris_slapstic.h"
#include "atetris_video.h"
#include "../../cpus/m6502/m6502.h"
#include "../machineBase.h"
#include "../../emulation/pokey.h"
#include "../../emulation/seqlock.h"

// Atari Tetris, MAME atetris (set 1), atari/atetris.cpp. One M6502 at
// 14.318181 MHz / 8, slapstic 137412-101 banking 0x4000-0x7fff, two POKEYs
// (sound; ALLPOT reads IN0/IN1), 2804 EEPROM. 336x240 ROT0: one 64x32
// tilemap of 8x8x4 tiles, 256 entry RGB 3-3-2 palette RAM, no sprites.
//
// Unrotated (as bublbobl): native lines 0..239 -> strips 3..32. Game layout
// (castle tile 677 at row 0, column 19): native x 0..223, the player 1
// side, centered. Other screens: 336 -> 240, columns with x % 7 == 2 or 5 dropped.
class atetris : public machineBase {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> atetris_rom;
  Asset<uint32_t[8], COMPRESSED> atetris_tiles;
public:
  atetris();
  uint8_t machineType() override { return MCH_ATETRIS; }
  void renderAudio(int16_t *out, int n) override {
    Pokey::renderBank(pokey, soundregs, out, n);
    audioClampAll(out, n, AUDIO_MAX);
  }
  void reset() override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  const int renderWidth() override { return atetris_video::OUT_WIDTH; }
  const int renderBuffer() override { return atetris_video::OUT_WIDTH * 2 * 8; }
  static const Logo &logo() { return atetris_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  uint8_t read(uint16_t a);
  void write(uint16_t a, uint8_t v);
  void run_until(uint32_t cycle);
  uint32_t cpu_clock() const;
  bool pokey_running(int n) const;
  uint8_t pokey_read(int n, uint8_t reg);
  void pokey_write(int n, uint8_t reg, uint8_t v);
  uint8_t in0() const;
  uint8_t in1() const;

  // 114 CPU cycles per line (456 pixel clocks / 4), 262 lines
  static const int LINES = 262;
  static const uint32_t CYCLES_PER_FRAME = 114 * LINES;

  m6502_t m_cpu;
  Slapstic101 slapstic;
  const unsigned char *rom = nullptr;
  const uint32_t (*tiles)[8] = nullptr;
  uint32_t frame_cycles = 0;     // cycles run in the current frame
  uint32_t total_cycles = 0;     // cycles of all finished frames (POKEY clock = CPU clock)
  uint32_t random_base[2] = {};  // cpu_clock() when SKCTL left reset, per POKEY
  uint8_t allpot[2] = {};        // ALLPOT latch, read back while in reset
  bool in_vblank = false;
  bool eeprom_unlocked = false;  // one write after 0x3400, then locked again
  uint8_t coin_leds = 0;         // frames left of coin LED flash
  uint8_t led_frame = 0;         // gameLeds() animation counter

  // memory[]: 0x0000-0x1fff as the CPU sees it (work RAM, video RAM), then
  // palette and EEPROM. soundregs: POKEY n at n * Pokey::REG_COUNT.
  static const uint16_t VIDEO_RAM = 0x1000;
  static const uint16_t PALETTE_RAM = 0x2000;
  static const uint16_t EEPROM_RAM = 0x2100;
  static const uint16_t EEPROM_SIZE = 0x200;
  static_assert(EEPROM_RAM + EEPROM_SIZE <= RAMSIZE, "EEPROM exceeds memory");

  // visible tile rows/columns and palette as of the line 240 IRQ, see
  // seqlock.h. The mode tile is read from the same copy, so mode and
  // picture always come from one frame.
  struct VideoState {
    uint8_t vram[atetris_video::VIS_ROWS][atetris_video::VIS_COLS * atetris_video::CELL_BYTES];
    uint8_t palette[256];
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  // per frame render state built by prepare_frame()
  atetris_video::Span crop_spans[atetris_video::VIS_COLS];
  atetris_video::Span scale_spans[atetris_video::VIS_COLS];
  const atetris_video::Span *spans = scale_spans;
  int span_cols = atetris_video::VIS_COLS; // tile columns with any kept pixel
  uint16_t pal[256];

private:
  Pokey pokey[2] = {Pokey(Pokey::CLOCKS_1789KHZ), Pokey(Pokey::CLOCKS_1789KHZ)};
};

#endif
