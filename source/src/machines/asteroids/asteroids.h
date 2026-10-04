#ifndef ASTEROIDS_H
#define ASTEROIDS_H

#include "asteroids_logo.h"
#include "asteroids_dipswitches.h"
#include "../../cpus/m6502/m6502.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/vector2d.h"

// Atari Asteroids, MAME asteroid (rev 4), atari/asteroid.cpp.
// One M6502 at 12.096 MHz / 8, DVG vector display 1024x768, discrete sound,
// ROT0. The picture stays upright, scaled to the panel width and centered
// vertically:
//
//   native (landscape)            panel (portrait, 240 x 288)
//   y 906 +-----------+           +-----------------+ row 0
//         |           |           +-----------------+ row 51  = y 906
//         |           |    ->     |    185 high     |
//   y 118 +-----------+           +-----------------+ row 235 = y 118
//         x 0      x 1023         +-----------------+ row 287
//                                 col 0 = x 0, col 239 = x 1023
class asteroids : public machineBase {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> asteroids_rom;
  Asset<unsigned char, COMPRESSED> asteroids_vrom;
public:
  asteroids();
  signed char machineType() override { return MCH_ASTEROIDS; }
  void start() override;
  void reset() override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  const int renderWidth() override { return 240; }
  const int renderBuffer() override { return 240 * 2 * 8; }
  static Asset<unsigned short, COMPRESSED> &logo() { return asteroids_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

  // keep in sync with romconv/internal/asteroids/asteroids_rom_convert.py
  static const uint16_t MAX_LINES = 1024;

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  uint8_t in0(uint8_t bit) const;
  uint8_t in1(uint8_t bit) const;
  bool in_game() const;
  uint16_t map_lines(uint16_t count);

  static const uint32_t CYCLES_PER_FRAME = 25200; // 1.512 MHz / 60 Hz
  static const uint32_t CYCLES_PER_NMI = 6144;    // 3 kHz / 12 = 246 Hz
  static const uint8_t STRIPS = 36;

  m6502_t m_cpu;
  const unsigned char *rom = nullptr, *vrom = nullptr;
  uint32_t cycles = 0;   // CPU clock since reset
  uint32_t next_nmi = 0; // cycles value of the next NMI
  bool ramsel = false;   // outlatch bit 2: swap player RAM pages 2 and 3
  uint8_t lamps = 0;     // outlatch bits 0-1: START lamps, active high here

  // vector RAM 0x4000-0x47ff as of the last DVG GO (the moment the real DVG
  // starts drawing it), see seqlock.h
  struct VideoState {
    uint8_t vram[0x800];
  };
  Seqlock<VideoState> go_latch;
  VideoState video = {};

  // per frame line list, built by prepare_frame(); Arena
  vec2d::Line *lines = nullptr;
  uint16_t *line_next = nullptr, *line_active = nullptr, *strip_head = nullptr;
  vec2d::StripRasterizer raster;
  uint16_t palette[16]; // intensity -> RGB565, byte swapped
  uint16_t *glow_carry = nullptr; // one per column, Arena, see ASTEROIDS_GLOW
  uint16_t glow[16];              // intensity -> glow RGB565, byte swapped
};

#endif
