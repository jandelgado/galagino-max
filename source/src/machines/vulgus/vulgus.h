#ifndef VULGUS_H
#define VULGUS_H

#include "../machineBase.h"
#include "../../emulation/ay8910.h"
#include "../../emulation/seqlock.h"
#include "vulgus_dipswitches.h"
#include "vulgus_logo.h"

// Vulgus (Capcom 1984), MAME capcom/vulgus.cpp, set vulgus.
// main Z80 + sound Z80 @ 3 MHz, 2x AY-3-8910 @ 1.5 MHz.
// Video ROT270: native (X, Y), visible Y 16..239 -> display line 255 - X,
// column Y - 16 (tilemap space after MAME scrolldx 128 / scrolldy 6).

// memory[] offsets
#define VULGUS_RAM      0x0000   // main 0xE000-0xEFFF
#define VULGUS_FG       0x1000   // main 0xD000-0xD7FF: code, attr at +0x400
#define VULGUS_BG       0x1800   // main 0xD800-0xDFFF: code, attr at +0x400
#define VULGUS_SPR      0x2000   // main 0xCC00-0xCC7F
#define VULGUS_SND_RAM  0x2080   // sound 0x4000-0x47FF
#define VULGUS_RAM_END  0x2880
static_assert(VULGUS_RAM_END <= RAMSIZE, "vulgus RAM exceeds memory[]");

#define VULGUS_SPRITE_BYTES  0x80
#define VULGUS_SLICES        8                        // sound IRQ 8x per frame
#define VULGUS_MAIN_STEPS    (INST_PER_FRAME * 4 / 3 / 2)
#define VULGUS_SND_STEPS     (INST_PER_FRAME / 2)

class vulgus : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> vulgus_rom_main;
  Asset<unsigned char, COMPRESSED> vulgus_rom_audio;
  Asset<unsigned short[8], COMPRESSED> vulgus_chars;
  Asset<uint32_t[16][2], COMPRESSED> vulgus_tiles;
  Asset<uint32_t[16][2], COMPRESSED> vulgus_sprites;
  Asset<uint16_t, COMPRESSED> vulgus_rgb;
  Asset<unsigned char, COMPRESSED> vulgus_lut;

public:
  vulgus();
  signed char machineType() override { return MCH_VULGUS; }
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
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
  static const Logo &logo(void) { return vulgus_logo; }

private:
  struct VideoState {
    uint8_t fg[0x800], bg[0x800];
    uint8_t sprites[VULGUS_SPRITE_BYTES];
    uint16_t scroll_x, scroll_y;
    uint8_t palette_bank;
  };

  uint8_t read_io(uint16_t addr);
  void publish_video();
  void build_cmaps(uint8_t bank);
  void bg_line(uint16_t *dst, int x);
  void fg_line(uint16_t *dst, int x);
  void blit_sprite(short row, int code, uint8_t color, int sx, int sy);

  Seqlock<VideoState> vblank;
  VideoState video;

  uint8_t scroll_lo[2] = {0, 0}, scroll_hi[2] = {0, 0};   // [0] y, [1] x
  uint8_t palette_bank = 0;
  uint8_t soundlatch = 0;
  uint8_t ay_addr[2] = {0, 0};

  const uint8_t *main_rom = nullptr, *audio_rom = nullptr;
  const uint16_t (*chars)[8] = nullptr;
  const uint32_t (*tiles)[16][2] = nullptr;
  const uint32_t (*sprite_gfx)[16][2] = nullptr;
  const uint16_t *rgb = nullptr;
  const uint8_t *lut = nullptr;

  uint16_t cmap_chars[64][4];
  uint8_t char_opaque[64];        // bit p set = char pen p opaque (lut != 15)
  uint16_t cmap_sprites[16][16];
  uint16_t cmap_tiles[32][8];     // current bank
  int16_t cmap_bank = -1;

  static const uint8_t AY_INC = 8;    // 1.5 MHz, as 1942
  static const uint8_t AY_GAIN = 5;
  Ay8910 ay[2] = {Ay8910(AY_INC, AY_GAIN), Ay8910(AY_INC, AY_GAIN)};
};

#endif
