#ifndef ARKANOID_H
#define ARKANOID_H

#include "../machineBase.h"
#include "../../emulation/ay8910.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/spinner.h"
#include "arkanoid_dipswitches.h"
#include "arkanoid_logo.h"

// Arkanoid (Taito 1986), MAME taito/arkanoid.cpp, set arkangc: Game
// Corporation bootleg without the 68705 MCU, paddle read directly at
// 0xd018. Z80 6 MHz, IRQ (RST 38) on vblank, YM2149 1.5 MHz. ROT90:
// native 256x224 (visible Y 16..239) -> native X = panel line - 16,
// native Y = 239 - panel column.
// Controls: stick left/right turns the paddle knob (emulation/spinner.h),
// FIRE launches the ball / fires the laser.

// memory[] offsets
#define ARKANOID_RAM      0x0000   // Z80 0xC000-0xC7FF
#define ARKANOID_VRAM     0x0800   // Z80 0xE000-0xE7FF: 32x32 tiles, 2 bytes each
#define ARKANOID_SPR      0x1000   // Z80 0xE800-0xE83F: 16 sprites, 4 bytes each
#define ARKANOID_RAM2     0x1040   // Z80 0xE840-0xEFFF
#define ARKANOID_RAM_END  0x1800
static_assert(ARKANOID_RAM_END <= RAMSIZE, "arkanoid RAM exceeds memory[]");

class arkanoid : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> arkanoid_rom;
  Asset<uint32_t[8], COMPRESSED> arkanoid_tiles;
  Asset<unsigned short, COMPRESSED> arkanoid_tile_map;
  Asset<unsigned short, COMPRESSED> arkanoid_rgb;

public:
  static const int WIDTH = 224;
  // paddle knob in quarter counts per frame, 1 count = 2 px: a tap moves
  // 2 px, held it ramps to 5 px per frame and crosses the 179 px
  // playfield in ~38 frames
  static const uint8_t SPIN_MIN = 4, SPIN_MAX = 10, SPIN_ACCEL = 1;

  arkanoid();
  uint8_t machineType() override { return MCH_ARKANOID; }
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
  const int renderWidth() override { return WIDTH; }
  const int renderBuffer() override { return WIDTH * 2 * 8; }
  void reset() override;
  void start() override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
  static const Logo &logo(void) { return arkanoid_logo; }

private:
  struct VideoState {
    uint8_t vram[0x800];
    uint8_t sprites[0x40];
    uint8_t ctrl;          // d008: b5 gfx bank, b6 palette bank
  };

  void publish_video();
  void blit_sprite(short row, const uint8_t *s);

  Seqlock<VideoState> vblank;
  VideoState video;

  bool irq_pending = false;
  uint8_t ctrl = 0;        // d008 latch
  uint8_t ay_addr = 0;
  Spinner paddle{SPIN_MIN, SPIN_MAX, SPIN_ACCEL};

  const uint8_t *rom = nullptr;
  const uint32_t (*tiles)[8] = nullptr;
  const uint16_t *tile_map = nullptr;
  const uint16_t *rgb = nullptr;

  static const uint8_t AY_INC = 8;   // 1.5 MHz / 8 / 24 kHz
  static const uint8_t AY_GAIN = 5;
  Ay8910 ay[1] = {Ay8910(AY_INC, AY_GAIN)};
};

#endif
