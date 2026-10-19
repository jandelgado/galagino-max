#ifndef CAVENGER_H
#define CAVENGER_H

#include "../machineBase.h"
#include "../../emulation/sn76489.h"
#include "../../emulation/seqlock.h"
#include "cavenger_dipswitches.h"
#include "cavenger_logo.h"

// Cosmic Avenger (Universal 1981), MAME universal/ladybug.cpp, set
// cavenger: Lady Bug board, ROT0. Z80 4 MHz, 2x SN76489 4 MHz, vblank
// polled (IN1 bits 6/7), coin -> NMI. Native visible X 8..247, Y 32..223
// -> panel column X - 8, line Y - 32 + 48 (centered in 288 lines).

// memory[] offsets
#define CAVENGER_RAM      0x0000   // Z80 0x6000-0x6FFF
#define CAVENGER_SPR      0x1000   // Z80 0x7000-0x73FF
#define CAVENGER_BG       0x1400   // Z80 0xD000-0xD7FF: code, color at +0x400
#define CAVENGER_RAM_END  0x1C00
static_assert(CAVENGER_RAM_END <= RAMSIZE, "cavenger RAM exceeds memory[]");

class cavenger : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> cavenger_rom;
  Asset<unsigned short[8], COMPRESSED> cavenger_chars;
  Asset<uint32_t[16], COMPRESSED> cavenger_sprites;
  Asset<unsigned short[8], COMPRESSED> cavenger_small;
  Asset<uint16_t, COMPRESSED> cavenger_rgb;
  Asset<unsigned char, COMPRESSED> cavenger_lut;

public:
  static const int WIDTH = 240;

  cavenger();
  signed char machineType() override { return MCH_CAVENGER; }
  void renderAudio(int16_t *out, int n) override { Sn76489::renderBank(sn, out, n); }
  const int renderWidth() override { return WIDTH; }
  const int renderBuffer() override { return WIDTH * 2 * 8; }
  signed char useVideoHalfRate() override { return 1; }   // as ladybug
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
  static const Logo &logo(void) { return cavenger_logo; }

private:
  struct VideoState {
    uint8_t bg[0x800];
    uint8_t sprites[0x400];
  };

  uint8_t read_input(uint16_t addr);
  void publish_video();
  void bg_line(uint16_t *dst, int y);
  void blit_sprite(short row, const uint8_t *s, int block);

  Seqlock<VideoState> vblank;
  VideoState video;

  uint8_t vblank_active = 0;   // IN1 bits 6/7
  uint8_t coin_prev = 0;

  const uint8_t *rom = nullptr;
  const uint16_t (*chars)[8] = nullptr;
  const uint32_t (*sprite_gfx)[16] = nullptr;
  const uint16_t (*small_gfx)[8] = nullptr;
  const uint16_t *rgb = nullptr;
  const uint8_t *lut = nullptr;

  uint16_t cmap_chars[8][4];
  uint16_t cmap_sprites[16][4];

  Sn76489 sn[2] = {Sn76489(Sn76489::INC_4MHZ, Sn76489::HOLD_SHORT_NOTES),
                   Sn76489(Sn76489::INC_4MHZ, Sn76489::HOLD_SHORT_NOTES)};
};

#endif
