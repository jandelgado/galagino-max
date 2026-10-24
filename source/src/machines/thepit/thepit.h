#ifndef THEPIT_H
#define THEPIT_H

#include "thepit_dipswitches.h"
#include "thepit_video.h"

#include "../machineBase.h"
#include "../../emulation/ay8910.h"
#include "../../emulation/seqlock.h"

// ============================================================
// The Pit (Zilec 1982), MAME thepit.cpp
// Main CPU (Z80 @ 3.072 MHz), NMI at vblank:
//   0x0000-0x4fff ROM
//   0x8000-0x87ff RAM
//   0x8800-0x8bff color RAM (mirror 0x8c00)
//   0x9000-0x93ff video RAM (mirror 0x9400)
//   0x9800-0x983f attributes, even bytes = column scroll (mirror 0x99xx-0x9fxx)
//   0x9840-0x985f sprite RAM, 8 x 4 bytes
//   0x9860-0x98ff RAM
//   0xa000 R IN0 stick + fire, active high (inverted by LS157)
//   0xa800 R IN1 coin + start, active high
//   0xb000 R DSW
//   0xb000-0xb007 W LS259: 0 NMI enable, 3 sound enable, 6/7 flip x/y
//   0xb800 R watchdog, W sound latch
// Sound CPU (Z80 @ 2.5 MHz, IM 1, IRQ once per frame):
//   0x0000-0x07ff ROM, 0x3800-0x3bff RAM
//   IO 0x00 W clear latch, 0x8e W AY address, 0x8f R/W AY data
//   AY-3-8910 @ 1.536 MHz, port A = sound latch. AY2 (0x8c/0x8d)
//   is never used by the thepit sound ROM.
// ============================================================

class thepit : public machineBase
{
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> thepit_main_rom;
  Asset<unsigned char, COMPRESSED> thepit_sound_rom;
  Asset<unsigned short[8], COMPRESSED> thepit_tiles;
  Asset<uint32_t[16], COMPRESSED> thepit_sprites;
  Asset<unsigned short[4], COMPRESSED> thepit_cmap;

public:
  thepit();

  uint8_t machineType() override { return MCH_THEPIT; }
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
  void reset() override;
  void start() override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char inZ80(unsigned short Port) override;
  void outZ80(unsigned short Port, unsigned char Value) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  void blit_sprite(short row, unsigned char s) override;

private:
  static constexpr uint8_t MAIN_CPU = 0;
  static constexpr uint8_t SOUND_CPU = 1;

  static constexpr uint16_t MAIN_ROM_SIZE = 0x5000;
  static constexpr uint16_t SND_ROM_SIZE  = 0x0800;

  static constexpr uint16_t RAM_ADDR     = 0x8000;
  static constexpr uint16_t CRAM_ADDR    = 0x8800;   // + mirror, 0x800 window
  static constexpr uint16_t VRAM_ADDR    = 0x9000;   // + mirror, 0x800 window
  static constexpr uint16_t OBJRAM_ADDR  = 0x9800;   // attributes, sprites, RAM
  static constexpr uint16_t OBJ_WINDOW   = 0x0800;   // attribute mirrors up to 0x9fff
  static constexpr uint16_t SND_RAM_ADDR = 0x3800;

  static constexpr uint16_t RAM_SIZE      = 0x0800;
  static constexpr uint16_t CRAM_SIZE     = 0x0400;
  static constexpr uint16_t VRAM_SIZE     = 0x0400;
  static constexpr uint16_t OBJRAM_SIZE   = 0x0100;
  static constexpr uint16_t ATTR_SIZE     = 0x0040;
  static constexpr uint16_t SPRITE_OFFSET = 0x0040;  // inside object RAM
  static constexpr uint16_t SPRITE_SIZE   = 0x0020;
  static constexpr uint16_t SND_RAM_SIZE  = 0x0400;
  static constexpr uint8_t  SPRITE_COUNT  = SPRITE_SIZE / 4;

  static constexpr uint16_t MAIN_RAM_OFFSET  = 0x0000;
  static constexpr uint16_t COLOR_RAM_OFFSET = MAIN_RAM_OFFSET + RAM_SIZE;
  static constexpr uint16_t VIDEO_RAM_OFFSET = COLOR_RAM_OFFSET + CRAM_SIZE;
  static constexpr uint16_t OBJRAM_OFFSET    = VIDEO_RAM_OFFSET + VRAM_SIZE;
  static constexpr uint16_t SND_RAM_OFFSET   = OBJRAM_OFFSET + OBJRAM_SIZE;
  static constexpr uint16_t MEM_FREE         = SND_RAM_OFFSET + SND_RAM_SIZE;
  static_assert(MEM_FREE <= RAMSIZE, "RAMSIZE too low for thepit");

  static constexpr uint16_t IN0_ADDR      = 0xa000;
  static constexpr uint16_t IN1_ADDR      = 0xa800;
  static constexpr uint16_t DSW_ADDR      = 0xb000;
  static constexpr uint16_t LATCH_ADDR    = 0xb000;   // 0xb000-0xb007
  static constexpr uint16_t WATCHDOG_ADDR = 0xb800;
  static constexpr uint16_t SNDLATCH_ADDR = 0xb800;

  static constexpr uint8_t LATCH_NMI_ENABLE   = 0;
  static constexpr uint8_t LATCH_SOUND_ENABLE = 3;

  // IN0 / IN1, active high
  static constexpr uint8_t IN0_LEFT   = 0x01;
  static constexpr uint8_t IN0_RIGHT  = 0x02;
  static constexpr uint8_t IN0_DOWN   = 0x04;
  static constexpr uint8_t IN0_UP     = 0x08;
  static constexpr uint8_t IN0_FIRE   = 0x10;
  static constexpr uint8_t IN1_COIN   = 0x01;
  static constexpr uint8_t IN1_START1 = 0x04;

  static constexpr uint8_t AY_ADDR_PORT   = 0x8e;
  static constexpr uint8_t AY_DATA_PORT   = 0x8f;
  static constexpr uint8_t SND_CLEAR_PORT = 0x00;
  static constexpr uint8_t AY_PORT_A      = 14;
  static constexpr uint8_t AY_VOLUME_A    = 8;     // registers 8-10: channel volumes
  static constexpr uint8_t AY_VOLUME_C    = 10;

  // cached Asset data(); hot paths read these per access
  const unsigned char *main_rom = nullptr;
  const unsigned char *sound_rom = nullptr;
  const unsigned short (*tiles)[8] = nullptr;
  const uint32_t (*sprites)[16] = nullptr;
  const unsigned short *colors = nullptr;   // 32 palette pens + 8 background

  // set in start(): memory is only valid after init()
  uint8_t *ram = nullptr;
  uint8_t *cram = nullptr;
  uint8_t *vram = nullptr;
  uint8_t *objram = nullptr;
  uint8_t *snd_ram = nullptr;

  uint8_t sound_latch = 0;
  uint8_t ay_addr = 0;
  uint8_t snd_irq_pending = 0;
  uint8_t sound_enabled = 0;
  uint8_t led_flash = 0;      // gameLeds() frames left

  // sprite[] holds low priority sprites first, high from first_high on
  int first_high = 0;

  // pixels of a priority tile in the current strip
  struct Span {
    int16_t x0, x1;
    uint16_t color;
  };

  // Tile, color, scroll and sprite RAM as of the main CPU's vblank NMI,
  // handed to the video core via Seqlock (see seqlock.h). Render reads
  // video.* only. Tile RAM is included so scroll and tiles never come
  // from different frames (edge garbage, see scramble).
  struct VideoState {
    uint8_t vram[VRAM_SIZE];
    uint8_t cram[CRAM_SIZE];
    uint8_t attr[ATTR_SIZE];
    uint8_t sprite[SPRITE_SIZE];
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  uint8_t *objram_at(uint16_t addr);
  uint8_t in0();
  uint8_t in1();
  void latch_w(uint8_t bit, uint8_t on);
  uint8_t draw_tiles(short row, Span *prio);
  void fill_span(const Span &span);
  uint8_t draw_sprites(short row, int from, int to);

private:
  static const uint8_t AY_INC = 8;
  static const uint8_t AY_GAIN = 11;
  Ay8910 ay[1] = {Ay8910(AY_INC, AY_GAIN)};
};

#endif
