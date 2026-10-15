#ifndef BERZERK_H
#define BERZERK_H

#include "berzerk_logo.h"
#include "berzerk_dipswitches.h"
#include "berzerk_audio.h"
#include "berzerk_video.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"

// Stern Berzerk (1980, MAME berzerk, RC31A). Z80 @ 2.5 MHz, 256 x 224
// 1bpp bitmap with 4 x 4 px color cells, ROT0. Sound: S14001A speech and
// Exidy 6840 effects, see berzerk_audio.h.
//
// Native 256 columns are shown in 240 (berzerk_video::buildColumnMap):
// 4 px always black borders cropped, 8 column pairs OR-merged.
class berzerk : public machineBase
{
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> berzerk_rom;
  Asset<unsigned char, COMPRESSED> berzerk_speech;

public:
  berzerk();

  signed char machineType() override { return MCH_BERZERK; }
  const int renderWidth() override { return berzerk_video::OUT_WIDTH; }
  const int renderBuffer() override { return berzerk_video::OUT_WIDTH * 2 * 8; }
  void reset() override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char inZ80(unsigned short Port) override;
  void outZ80(unsigned short Port, unsigned char Value) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  // S14001A speech + Exidy 6840, mixed on the emulation core
  void renderAudio(int16_t *out, int n) override {
    audio.render(out, n);
    for (int i = 0; i < n; i++) {
      out[i] = audioClamp(out[i] >> MIX_SHIFT, AUDIO_MAX);
    }
  }
  static const int MIX_SHIFT = 6;  // int16 -> +-512
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  // bitmap hardware, no tiles or sprites
  void blit_tile(short row, char col) override { }
  void blit_sprite(short row, unsigned char s) override { }

private:
  // memory[] layout
  enum : uint16_t { VRAM = 0x0000, CRAM = 0x2000, NVRAM = 0x2800, RAM_END = 0x2c00 };
  static_assert(RAM_END <= RAMSIZE, "berzerk RAM exceeds RAMSIZE");

  // video input latched at vblank: visible bitmap lines and their color rows
  struct VideoState {
    uint8_t bitmap[berzerk_video::VIS_LINES][32];
    uint8_t color[berzerk_video::VIS_LINES / 4][32];
  };

  uint8_t magicWrite(uint8_t cur, uint8_t data);
  uint8_t readDip(uint8_t port) const;

  const uint8_t *rom = nullptr;
  Seqlock<VideoState> *vblank = nullptr; // Arena
  VideoState *video = nullptr;           // Arena, render copy
  BerzerkAudio audio;
  int16_t column_map[256];
  uint16_t pens[16];

  uint16_t vpos = 0;
  uint8_t magic_ctrl = 0, last_shift = 0, intercept = 0;
  bool irq_enabled = false, irq_pending = false, nmi_enabled = false;

#ifdef LED_PIN
  uint8_t led_frame = 0;
#endif
};

#endif
