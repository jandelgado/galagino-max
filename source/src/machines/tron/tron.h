#ifndef TRON_H
#define TRON_H

#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/z80ctc.h"
#include "tron_ssio.h"
#include "tron_dipswitches.h"

// Tron (Bally Midway 1982), MAME midway/mcr.cpp: MCR 90010 CPU board (Z80
// at 2.496 MHz, IM 2 interrupts from a Z80 CTC), 91399 video gen, Super
// Sound I/O board (tron_ssio.h).
//
// The screen is 512x480 ROT90, but tiles are 8x8 doubled by the hardware,
// so galagino shows it at half resolution: native half-res (X 0-255,
// Y 0-239) -> panel row X + 16, column 239 - Y. Sprites (32x32) come
// downsampled to 16x16 from romconv.

// memory[] offsets
#define TRON_MEM_NVRAM   0x0000  // c000-c7ff, mirror 0x1800
#define TRON_MEM_SPRITES 0x0800  // e000-e1ff, mirror 0x1600
#define TRON_MEM_VIDEO   0x0a00  // e800-efff, mirror 0x1000
#define TRON_MEM_SSIO    0x1200  // SSIO 8000-83ff
#define TRON_MEM_END     (TRON_MEM_SSIO + TronSsio::RAM_SIZE)
static_assert(TRON_MEM_END <= RAMSIZE, "tron RAM does not fit");

class tron : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> tron_rom;
  Asset<unsigned char, COMPRESSED> tron_snd_rom;
  Asset<uint32_t[8], COMPRESSED> tron_tilemap;
  Asset<uint32_t[16][2], COMPRESSED> tron_spritemap;

public:
  tron();

  signed char machineType() override { return MCH_TRON; }
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

private:
  static const int OUT_WIDTH = 240;
  static const int TILE_COLS = 32;  // native X / 8
  static const int TILE_ROWS = 30;  // native Y / 8
  static const int TILE_BYTES = TILE_COLS * TILE_ROWS * 2;
  static const int SPRITE_COUNT = 128;
  static const int SPRITE_BYTES = SPRITE_COUNT * 4;
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
  void render_tiles(int tx);
  void render_sprites(int tx);

  Z80Ctc ctc;
  TronSsio ssio;
  uint16_t palette9[COLORS] = {};  // 9 bit xRBG from video RAM writes
  bool odd_field = false;
  int main_debt = 0;

  // strip buffers, video core only
  uint8_t cat[OUT_WIDTH];       // sprite color bank (tile category << 4) per column
  uint8_t orbuf[8][OUT_WIDTH];  // ORed sprite pens per strip row

  uint64_t blank_sprites = 0;   // bit n: sprite code n has no pixels

  // Cached: hot path reads these per access.
  const unsigned char *main_rom = nullptr;
  const unsigned char *sound_rom = nullptr;
  const uint32_t (*tiles)[8] = nullptr;
  const uint32_t (*sprite_gfx)[16][2] = nullptr;
};

#endif
