#ifndef SOLOMON_H
#define SOLOMON_H

#include "../machineBase.h"
#include "../../emulation/ay8910.h"
#include "../../emulation/seqlock.h"
#include "solomon_dipswitches.h"
#include "solomon_logo.h"
#include "solomon_video.h"

// ============================================================
// Solomon's Key (Tecmo 1986), MAME tecmo/solomon.cpp.
// Main Z80 4 MHz, NMI at vblank gated by e600 b0. Sound Z80
// 3.072 MHz, IM 1 IRQ 120 Hz, NMI per sound command, 3x YM2149.
// ROT0 256x224 upright: 240 columns, native x 8..247 (default),
// every 16th column dropped (SOLOMON_SCALE) or a 240 px window
// following the player (SOLOMON_SCROLL).
// ============================================================

#if defined(SOLOMON_SCALE) && defined(SOLOMON_SCROLL)
#error "SOLOMON_SCALE and SOLOMON_SCROLL are exclusive"
#endif

// memory[] layout
#define SOL_WORK_RAM    0x0000  // c000-cfff
#define SOL_FG_COLOR    0x1000  // d000-d3ff
#define SOL_FG_VIDEO    0x1400  // d400-d7ff
#define SOL_BG_COLOR    0x1800  // d800-dbff
#define SOL_BG_VIDEO    0x1c00  // dc00-dfff
#define SOL_SPRITE_RAM  0x2000  // e000-e07f
#define SOL_PALETTE_RAM 0x2080  // e400-e5ff
#define SOL_AUDIO_RAM   0x2280  // sound 4000-47ff
#define SOL_MEM_END     0x2a80
static_assert(SOL_MEM_END <= RAMSIZE, "solomon RAM does not fit");

class solomon : public machineBase
{
  // ROM assets: COMPRESSED ones unpacked eagerly in the ctor, bg first
  Asset<unsigned char, PLAIN> solomon_main_rom;
  Asset<unsigned char, COMPRESSED> solomon_audio_rom;
  Asset<uint32_t[8], COMPRESSED> solomon_fg_tiles;
  Asset<uint32_t[8], COMPRESSED> solomon_bg_tiles;
  Asset<uint32_t[16][2], PLAIN> solomon_sprites;

public:
  solomon();

  void reset() override;
  signed char machineType() override { return MCH_SOLOMON; }
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
  const int renderWidth() override { return solomon_video::OUT_WIDTH; }
  const int renderBuffer() override { return solomon_video::OUT_WIDTH * 2 * 8; }

  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char inZ80(unsigned short Port) override;
  void outZ80(unsigned short Port, unsigned char Value) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void) { return solomon_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  // Render input, latched at vblank (see seqlock.h). Visible tile rows only.
  struct VideoState {
    uint8_t fg_color[solomon_video::VIS_TILES];
    uint8_t fg_video[solomon_video::VIS_TILES];
    uint8_t bg_color[solomon_video::VIS_TILES];
    uint8_t bg_video[solomon_video::VIS_TILES];
    uint8_t spr[solomon_video::SPRITE_BYTES];
  };
  Seqlock<VideoState> vblank;
  VideoState video;  // render copy, video core only

  unsigned char rdMain(unsigned short Addr);
  unsigned char rdSound(unsigned short Addr);
  void wrMain(unsigned short Addr, unsigned char Value);
  unsigned char readP1(void);
  unsigned char readSystem(void);
  unsigned char readE603(void);
  void writePalette(unsigned short offset, unsigned char value);
  void publishVideo(void);
  void prepare_frame_sprites(void);
  void followPlayer(void);
  void render_line(uint16_t *dst, int line);
  void outputLine(uint16_t *dst, const uint16_t *native, int line);

  uint16_t palette[256];            // RGB565 swapped, written by the emulation core
#ifdef SOLOMON_SCALE
  uint8_t x_map[solomon_video::OUT_WIDTH];  // output column -> native x
#endif
  uint8_t scroll_x = solomon_video::CROP_LEFT;  // native x of output column 0, video core only
  bool nmi_mask = false;            // e600 b0
  bool audio_irq = false;           // irq0_line_hold: pending until IFF1
  bool audio_nmi = false;           // sound command
  uint8_t sound_latch = 0;
  uint8_t ay_address[3] = {0, 0, 0};
#ifdef LED_PIN
  uint8_t led_frame = 0;
#endif

  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *main_rom = nullptr;
  const unsigned char *audio_rom = nullptr;
  const uint32_t (*fg_tiles)[8] = nullptr;
  const uint32_t (*bg_tiles)[8] = nullptr;
  const uint32_t (*sprites)[16][2] = nullptr;

  static const uint8_t AY_INC = 8;   // 1.5 MHz, as pbaction
  static const uint8_t AY_GAIN = 4;
  Ay8910 ay[3] = {Ay8910(AY_INC, AY_GAIN), Ay8910(AY_INC, AY_GAIN), Ay8910(AY_INC, AY_GAIN)};
};

#endif
