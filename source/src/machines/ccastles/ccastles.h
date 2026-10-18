#ifndef CCASTLES_H
#define CCASTLES_H

#include "ccastles_logo.h"
#include "ccastles_dipswitches.h"
#include "ccastles_video.h"
#include "../../cpus/m6502/m6502.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"

// Atari Crystal Castles, joystick version (MAME ccastlesj), atari/ccastles.cpp.
// One M6502 at 10 MHz / 8, two POKEYs (POKEY 2 ALLPOT reads the DIPs),
// 2 x X2212 NOVRAM. 256x232 ROT0 4 bpp bitmap in 32 KB DRAM (also work RAM,
// every write through the write protect PROM), 40 8x16x3 sprites merged by
// the priority PROM, 32 pen palette RAM.
//
// Unrotated: native lines 24..255 -> strips 4..32, native x 8..247 -> 240
// columns (edges hold no game content).
class ccastles : public machineBase {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> ccastles_rom;
  Asset<uint32_t[16], COMPRESSED> ccastles_sprites;
  Asset<unsigned char, COMPRESSED> ccastles_proms;
public:
  ccastles();
  signed char machineType() override { return MCH_CCASTLES; }
  void reset() override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  const int renderWidth() override { return ccastles_video::OUT_WIDTH; }
  const int renderBuffer() override { return ccastles_video::OUT_WIDTH * 2 * 8; }
  static const Logo &logo() { return ccastles_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  uint8_t read(uint16_t a);
  void write(uint16_t a, uint8_t v);
  void write_io(uint16_t a, uint8_t v);
  void run_until(uint32_t cycle);
  uint32_t cpu_clock() const;
  bool pokey_running(int n) const;
  uint8_t pokey_read(int n, uint8_t reg);
  void pokey_write(int n, uint8_t reg, uint8_t v);
  uint8_t in0() const;
  uint8_t leta(uint8_t n) const;

  // 80 CPU cycles per line (320 pixel clocks / 4), 256 lines
  static const int LINES = 256;
  static const int LINE_STEP = 8;
  static const int IRQ_LINES = 64; // sync PROM IRQ edges at 0, 64, 128, 192
  static const uint32_t CYCLES_PER_LINE = 80;
  static const uint32_t CYCLES_PER_FRAME = CYCLES_PER_LINE * LINES;
  static const uint32_t VRAM_SIZE = 0x8000;

  m6502_t m_cpu;
  const unsigned char *rom = nullptr;
  const unsigned char *wpprom = nullptr;
  const unsigned char *priprom = nullptr;
  const uint32_t (*sprites)[16] = nullptr;
  uint8_t *vram = nullptr;       // 32 KB DRAM in the Arena
  uint8_t bitmode_xy[2] = {};
  uint8_t latch0 = 0;            // 8N outputs, bit n = Qn
  uint8_t latch1 = 0;            // 6P outputs, bit n = Qn
  uint8_t hscroll = 0;
  uint8_t vscroll = 0;
  uint16_t pal[32] = {};         // palette RAM as RGB565
  uint32_t frame_cycles = 0;     // cycles run in the current frame
  uint32_t total_cycles = 0;     // cycles of all finished frames (POKEY clock = CPU clock)
  uint32_t random_base[2] = {};  // cpu_clock() when SKCTL left reset, per POKEY
  uint8_t allpot[2] = {};        // ALLPOT latch, read back while in reset
  bool in_vblank = false;
  uint8_t coin_leds = 0;         // frames left of coin LED flash
  uint8_t led_frame = 0;         // gameLeds() animation counter

  // memory[]: 0x8000-0x8fff (static + sprite RAM), then NOVRAM
  static const uint16_t STATIC_RAM = 0x0000;
  static const uint16_t SPRITE_RAM = 0x0e00; // 0x8e00 buffer 2, 0x8f00 buffer 1
  static const uint16_t SPRITE_BUF_SIZE = 0x100;
  static const uint16_t NOVRAM = 0x1000;
  static const uint16_t NOVRAM_SIZE = 0x100;
  static_assert(NOVRAM + NOVRAM_SIZE <= RAMSIZE, "NOVRAM exceeds memory");

  // sprites, palette, scroll as of vblank end (line 24), see seqlock.h. DRAM is
  // 32 KB and stays live.
  struct VideoState {
    uint8_t sprites[ccastles_video::SPRITE_BYTES];
    uint16_t pal[32];
    uint8_t hscroll, vscroll;
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  uint8_t merge[256];            // (mo << 4 | pix) -> pen, from the priority PROM
  uint16_t lut[256];             // merge mapped through video.pal, per frame
  uint8_t mo[8][256];            // sprite pens of the current strip
};

#endif
