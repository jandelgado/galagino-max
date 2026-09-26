#ifndef CENTIPEDE_H
#define CENTIPEDE_H

#include "centipede_logo.h"
#include "centipede_dipswitches.h"
#include "../../cpus/m6502/m6502.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"

// Atari Centipede, MAME centiped3 (revision 3), atari/centiped.cpp.
// One M6502 at 12.096 MHz / 8, POKEY sound, 256x240 screen, ROT270.
class centipede : public machineBase {
public:
  centipede();
  ~centipede();
  signed char machineType() override { return MCH_CENTIPEDE; }
  void start() override;
  void reset() override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  const int renderWidth() override { return 240; }
  const int renderBuffer() override { return 240 * 2 * 8; }
  static RomData<unsigned short, COMPRESSED> &logo() { return centipede_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  void run_until(uint32_t cycle);
  bool pokey_running() const;
  uint32_t cpu_clock() const;
  uint8_t in1() const;
  uint8_t in3() const;
  void earom_control(uint8_t v);
  void earom_update();
  void render_line(uint16_t *line, int x) const;

  static const uint32_t CYCLES_PER_FRAME = 25200; // 1.512 MHz / 60 Hz
  static const int LINES = 263;

  m6502_t m_cpu;
  const unsigned char *rom = nullptr, *gfx = nullptr;
  uint32_t frame_cycles = 0; // cycles run in the current frame
  uint32_t total_cycles = 0; // cycles of all finished frames (POKEY clock)
  uint32_t random_base = 0;  // cpu_clock() when SKCTL left reset
  bool in_vblank = false;
  uint8_t palette_ram[16] = {};
  bool start_lamp = false;   // outlatch 3, START1 lamp: off in attract, blinks with credits
  uint8_t coin_leds = 0;     // frames left of coin LED flash, set by outlatch 0-2
  uint8_t led_frame = 0;     // gameLeds() animation counter
  uint8_t worm_pal = 15;     // palette entry of last seen centipede body

  // ER2055 high score EAROM, 64 bytes, volatile
  uint8_t earom[64];
  uint8_t earom_addr = 0, earom_data = 0, earom_state = 0;

  // Pre-rotated gfx, one word per (code, flipy, native column): 2 bits per
  // pixel along native y. Tiles: 64 codes (0x40..0x7f), 8 px. Sprites: 128
  // codes, 16 px. Built once in start().
  uint16_t *tile_cols = nullptr;
  uint32_t *sprite_cols = nullptr;

  // RAM 0x0400-0x07ff (playfield 0x3c0 + sprites 0x40) and palette as of
  // the line 240 vblank IRQ, see seqlock.h
  static const uint16_t VIDEO_RAM = 0x0400;
  static const uint16_t SPRITE_OFS = 0x03c0; // within VideoState::ram
  struct VideoState {
    uint8_t ram[0x400];
    uint8_t palette[16];
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  // per frame render state built by prepare_frame()
  struct Sprite {
    int16_t x, y;
    uint16_t base;         // index of column 0 in sprite_cols
    uint8_t flipx, opaque; // opaque: bit p set = pen p drawn
    uint16_t color[4];
  };
  Sprite spr[16];
  uint16_t tile_pal[4];
};

#endif
