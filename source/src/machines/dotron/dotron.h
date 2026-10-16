#ifndef DOTRON_H
#define DOTRON_H

#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/z80ctc.h"
#include "../mcr/mcr_ssio.h"
#include "dotron_aim.h"
#include "dotron_dipswitches.h"
#include "dotron_logo.h"

// Discs of Tron (Bally Midway 1983, upright 10/4/83), MAME midway/mcr.cpp:
// MCR 91490 CPU board (Z80 at 5 MHz, IM 2 interrupts from a Z80 CTC),
// 91464 sprite board, Super Sound I/O board (mcr/mcr_ssio.h).
//
// The screen is 512x480 landscape, ORIENTATION_FLIP_X, tiles 8x8 doubled
// by the hardware. galagino shows it at half resolution, mirrored: native
// (X 0-255, Y 0-239) -> panel column 247 - X (X 8-247 visible), row Y + 24.
// Sprites (32x32) come downsampled to 16x16 from romconv, one variant per
// flip, all graphics stored mirrored.

// memory[] offsets
#define DOTRON_MEM_NVRAM   0x0000  // e000-e7ff
#define DOTRON_MEM_SPRITES 0x0800  // e800-e9ff, mirror 0x200
#define DOTRON_MEM_VIDEO   0x0a00  // f000-f7ff
#define DOTRON_MEM_SSIO    0x1200  // SSIO 8000-83ff
#define DOTRON_MEM_END     (DOTRON_MEM_SSIO + McrSsio::RAM_SIZE)
static_assert(DOTRON_MEM_END <= RAMSIZE, "dotron RAM does not fit");

class dotron : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> dotron_rom;
  Asset<unsigned char, COMPRESSED> dotron_snd_rom;
  Asset<uint32_t[8], COMPRESSED> dotron_tilemap;
  Asset<uint32_t[16][2], COMPRESSED> dotron_spritemap;

public:
  dotron();

  signed char machineType() override { return MCH_DOTRON; }
  const int renderWidth() override { return OUT_WIDTH; }
  const int renderBuffer() override { return OUT_WIDTH * 2 * 8; }

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char inZ80(unsigned short Port) override;
  void outZ80(unsigned short Port, unsigned char Value) override;

  void reset() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  void renderAudio(int16_t *out, int n) override;
  static const Logo &logo(void) { return dotron_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const int OUT_WIDTH = 240;
  static const int TILE_COLS = 32;  // native X / 8
  static const int TILE_ROWS = 30;  // native Y / 8
  static const int TILE_BYTES = TILE_COLS * TILE_ROWS * 2;
  static const int SPRITE_COUNT = 128;
  static const int SPRITE_BYTES = SPRITE_COUNT * 4;
  static const int SPRITE_CODES = 128;
  static const int SPRITE_VARIANTS = 4;  // romconv sampling per flip
  static const int COLORS = 64;

  // Render input, latched where MAME draws the 30 Hz frame (see seqlock.h).
  struct VideoState {
    uint8_t tiles[TILE_BYTES];
    uint8_t sprites[SPRITE_BYTES];
    uint16_t pal[COLORS];  // RGB565, byte swapped
  };
  Seqlock<VideoState> vblank;
  VideoState video;  // render copy, video core only

  void publish_video();
  uint8_t read_input(uint8_t n);
  void render_tiles(int ty);
  void render_sprites(int ty);

  Z80Ctc ctc;
  McrSsio ssio;
  DotronAim aim;
  uint16_t palette9[COLORS] = {};  // 9 bit xRBG from palette RAM writes
  bool odd_field = false;
  uint8_t dial = 0;  // IP1 dial counter, 7 bit
  uint8_t last_command = 0;
  uint8_t led_flash = 0;  // frames left, set by a new sound command
  int main_debt = 0;

  // strip buffer, video core only: pixel taken by a higher sprite
  uint8_t claim[8][OUT_WIDTH];

  uint64_t blank_sprites[2] = {0, 0};  // bit n: sprite code n has no pixels

  // Cached: hot path reads these per access.
  const unsigned char *main_rom = nullptr;
  const unsigned char *sound_rom = nullptr;
  const uint32_t (*tiles)[8] = nullptr;
  const uint32_t (*sprite_gfx)[16][2] = nullptr;
};

#endif
