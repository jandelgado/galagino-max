#ifndef MILLIPEDE_H
#define MILLIPEDE_H

#include "millipede_logo.h"
#include "millipede_dipswitches.h"
#include "../../cpus/m6502/m6502.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/er2055.h"

// Atari Millipede, MAME milliped, atari/centiped.cpp. Centipede hardware
// with a new memory map, two tile banks, RAM palette and two POKEYs.
// One M6502 at 12.096 MHz / 8, 256x240 screen, ROT270.
class millipede : public machineBase {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> millipede_gfx;
  Asset<unsigned char, COMPRESSED> millipede_rom;
public:
  millipede();
  signed char machineType() override { return MCH_MILLIPEDE; }
  void start() override;
  void reset() override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  const int renderWidth() override { return 240; }
  const int renderBuffer() override { return 240 * 2 * 8; }
  static Asset<unsigned short, COMPRESSED> &logo() { return millipede_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  void run_until(uint32_t cycle);
  uint32_t cpu_clock() const;
  bool pokey_running(int n) const;
  uint8_t pokey_read(int n, uint8_t reg);
  void pokey_write(int n, uint8_t reg, uint8_t v);
  uint8_t in0() const;
  uint8_t in1() const;
  uint8_t in2() const;
  void render_line(uint16_t *line, int x) const;

  static const uint32_t CYCLES_PER_FRAME = 25200; // 1.512 MHz / 60 Hz
  static const int LINES = 263;

  m6502_t m_cpu;
  const unsigned char *rom = nullptr, *gfx = nullptr;
  uint32_t frame_cycles = 0;     // cycles run in the current frame
  uint32_t total_cycles = 0;     // cycles of all finished frames (POKEY clock)
  uint32_t random_base[2] = {};  // cpu_clock() when SKCTL left reset, per POKEY
  uint8_t allpot[2] = {};        // ALLPOT latch, read back while in reset
  bool in_vblank = false;
  bool tben = false;             // outlatch 5, low: P8 DIPs on IN0/IN1
  bool start_lamp = false;       // outlatch 3, START1 lamp: off in attract, blinks with credits
  uint8_t coin_leds = 0;         // frames left of coin LED flash, set by outlatch 0-2
  uint8_t led_frame = 0;         // gameLeds() animation counter
  uint8_t worm_pal = 0x13;       // palette entry of last seen millipede body
  uint8_t palette_ram[32] = {};
  Er2055 earom; // high score EAROM

  // Pre-rotated gfx, one word per (code, native column): 2 bits per pixel
  // along native y. Tiles: 128 codes (bank << 6 | code), 8 px. Sprites: 128
  // codes x flipy, 16 px. Built once in start().
  uint16_t *tile_cols = nullptr;
  uint32_t *sprite_cols = nullptr;

  // memory[]: work RAM 0x0000-0x03ff, then video RAM 0x1000-0x13ff
  // (playfield 0x3c0 + sprites 0x40). soundregs: POKEY n registers at
  // n * Pokey::REG_COUNT, layout in pokey.h (20 of 80 bytes).
  static const uint16_t VIDEO_RAM = 0x0400;
  static const uint16_t SPRITE_OFS = 0x03c0; // within VideoState::ram
  static_assert(VIDEO_RAM + 0x400 <= RAMSIZE, "video RAM exceeds memory");
  // video RAM and palette as of the line 240 vblank IRQ, see seqlock.h
  struct VideoState {
    uint8_t ram[0x400];
    uint8_t palette[32];
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  // per frame render state built by prepare_frame()
  struct Sprite {
    int16_t x, y;
    uint16_t base;  // index of column 0 in sprite_cols
    uint8_t opaque; // bit p set = pen p drawn
    uint16_t color[4];
  };
  Sprite spr[16];
  uint16_t tile_pal[16]; // 4 colour banks x 4 pens
};

#endif
