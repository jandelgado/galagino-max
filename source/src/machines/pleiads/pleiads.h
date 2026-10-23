#ifndef PLEIADS_H
#define PLEIADS_H

#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "pleiads_dipswitches.h"
#include "pleiads_logo.h"
#include "pleiads_sound.h"

// Pleiads (Tehkan 1981), MAME phoenix/phoenix.cpp, set pleiads: Phoenix
// board (8085A 5.5 MHz on the Z80 core, two tilemaps, bg x scroll, two
// video RAM pages), 4 palette banks, video register protection, own
// sound board. ROT90: native (X, Y), Y 0..207 -> panel row X + 16,
// column 215 - Y (208 px centered in 224).

class pleiads : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> pleiads_rom;
  Asset<unsigned short[8], COMPRESSED> pleiads_bgtiles;
  Asset<unsigned short[8], COMPRESSED> pleiads_fgtiles;
  Asset<uint16_t, COMPRESSED> pleiads_palette;

public:
  pleiads();
  uint8_t machineType() override { return MCH_PLEIADS; }
  void renderAudio(int16_t *out, int n) override { sound.render(soundregs, out, n); }
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
  static const Logo &logo(void) { return pleiads_logo; }

private:
  static const int PAGE = 0x1000;          // video RAM page: fg 0x000, bg 0x800
  static const int MAP_BYTES = 26 * 32;    // visible tile rows 0..25

  struct VideoState {
    uint8_t fg[MAP_BYTES], bg[MAP_BYTES];
    uint8_t scroll, palette_bank;
  };

  uint8_t read_in0();
  void write_videoreg(uint8_t v);
  void publish_video();
  void layer_line(uint16_t *dst, const uint8_t *map, const uint16_t (*gfx)[8],
                  int x, uint16_t pen_base, bool transparent);

  Seqlock<VideoState> vblank;
  VideoState video;

  uint8_t page = 0, palette_bank = 0, scroll = 0;
  uint8_t protection = 0;       // video register bits 2-7: protection question
  bool vblank_active = false;

  const uint8_t *rom = nullptr;
  const uint16_t (*bg_gfx)[8] = nullptr, (*fg_gfx)[8] = nullptr;
  const uint16_t *palette = nullptr;

  PleiadsSound sound;
};

#endif
