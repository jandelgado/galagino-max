#ifndef ELEVATOR_H
#define ELEVATOR_H

#include "../machineBase.h"
#include "../../emulation/ay8910.h"
#include "../../emulation/seqlock.h"
#include "elevator_dipswitches.h"
#include "elevator_logo.h"

// Elevator Action (Taito 1983), MAME taito/taitosj.cpp, set elevatorb:
// bootleg without the 68705 protection MCU (8800/8801 read 0/ff).
// Main Z80 4 MHz, IRQ at vblank:
//   0000-5fff ROM, 6000-7fff ROM bank (d50e b7), 8000-87ff RAM
//   9000-bfff character RAM (write only, gfx of tiles and sprites)
//   c000-cfff RAM + 3 tilemaps, d000-d05f column scroll, d100 sprites,
//   d200 palette, d300 priority, d4xx inputs / gfx ROM port / AY#0,
//   d5xx scroll, colors, sound latch, bank, d600 video mode
// Sound Z80 3 MHz: 0000-1fff ROM, 4000-43ff RAM, 48xx AY#1-3, 50xx latch.
// NMI on a latch write (masked by AY#3 port B b0) or semaphore 2, IRQ at
// 36.6 Hz. All 4 AY-3-8910 at 1.5 MHz.
// ROT0: native 256x224 (visible Y 16..239) -> 240 columns, 8 px cropped
// left and right, on panel strips 4..31.
// Controls: FIRE shoots, FIRE with UP held or B jumps.

class elevator : public machineBase {
  Asset<unsigned char, COMPRESSED> elevator_main_rom;
  Asset<unsigned char, COMPRESSED> elevator_sound_rom;
  Asset<unsigned char, COMPRESSED> elevator_gfx_rom;
  Asset<unsigned char, COMPRESSED> elevator_draw_order;

public:
  static const int WIDTH = 240;

  elevator();
  signed char machineType() override { return MCH_ELEVATOR; }
  const int renderWidth() override { return WIDTH; }
  const int renderBuffer() override { return WIDTH * 2 * 8; }
  void renderAudio(int16_t *out, int n) override;
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
  static const Logo &logo(void) { return elevator_logo; }

private:
  static uint16_t pen(uint8_t lo, uint8_t hi);

  static constexpr uint8_t MAIN_CPU = 0;
  static constexpr uint8_t SOUND_CPU = 1;

  // memory[] offsets
  static constexpr uint16_t RAM_OFS = 0x0000;      // 8000-87ff
  static constexpr uint16_t VRAM_OFS = 0x0800;     // c000-cfff: RAM, tilemaps 1-3
  static constexpr uint16_t COLSCROLL_OFS = 0x1800; // d000-d0ff
  static constexpr uint16_t SPR_OFS = 0x1900;      // d100-d1ff
  static constexpr uint16_t PAL_OFS = 0x1a00;      // d200-d27f, mirror d280
  static constexpr uint16_t PAL_SIZE = 0x80;
  static constexpr uint16_t SND_RAM_OFS = 0x1a80;  // sound 4000-43ff
  static constexpr uint16_t RAM_END = 0x1e80;
  static_assert(RAM_END <= RAMSIZE, "elevator RAM exceeds memory[]");

  static constexpr uint16_t CHARRAM_SIZE = 0x3000; // 9000-bfff
  static constexpr uint16_t TILEMAP_SIZE = 0x400;   // 32 x 32
  static constexpr uint16_t TILEMAP_OFS = VRAM_OFS + 0x400;
  static constexpr uint8_t LAYERS = 3;
  static constexpr uint8_t COLSCROLL_SIZE = 32 * LAYERS;
  static constexpr uint8_t SPR_SIZE = 0x80;          // one of two pages, 32 x 4
  static constexpr uint8_t PENS = 64;

  unsigned char rdMain(unsigned short Addr);
  void wrMain(unsigned short Addr, unsigned char Value);
  unsigned char rdSound(unsigned short Addr);
  void wrSound(unsigned short Addr, unsigned char Value);
  unsigned char rdIo(uint8_t reg);
  void wrIo(uint8_t reg, unsigned char Value);
  uint8_t in0(void);
  uint8_t in2(void);
  void updateNmi(void);
  void stepCpu(uint8_t c);
  bool isIdle(uint8_t c);

  // render inputs as of the vblank IRQ (seqlock.h); tile RAM with them,
  // else freshly written rows show at the scroll edge. Character RAM stays
  // live: the game rewrites it only between scenes.
  struct VideoState {
    uint8_t tilemap[LAYERS][TILEMAP_SIZE];
    uint8_t colscroll[COLSCROLL_SIZE];
    uint8_t spr[SPR_SIZE];
    uint8_t pal[PAL_SIZE];
    uint8_t scroll[6];
    uint8_t colorbank[2];
    uint8_t mode;
    uint8_t prio;
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};
  uint16_t palette[PENS];

  void publishVideo(void);
  void prepareSprites(void);
  void drawLayer(uint8_t w, uint8_t y0);
  void drawSprites(uint8_t y0);
  void drawSpriteLine(const sprite_S &s, uint8_t y, uint16_t *dst);

  const uint8_t *main_rom = nullptr;
  const uint8_t *sound_rom = nullptr;
  const uint8_t *gfx_rom = nullptr;
  const uint8_t *draw_order = nullptr;  // [d300 & 0x1f][4], back to front
  uint8_t *charram;                     // Arena

  // main CPU registers (d5xx, d300, d600)
  uint8_t scroll[6] = {};
  uint8_t colorbank[2] = {};
  uint8_t video_mode = 0;
  uint8_t priority = 0;
  uint16_t gfx_ptr = 0;
  uint8_t bank_ctrl = 0;   // d50e: b7 ROM bank, b1 sound on

  // sound CPU interface
  uint8_t soundlatch = 0;
  bool latch_full = false;
  bool sema2 = false;
  bool nmi_line = false;
  bool nmi_pending = false;
  bool main_irq = false;
  bool sound_irq = false;
  uint16_t sound_irq_slices = 0;
  uint8_t ay_addr[4] = {};  // register select per chip, regs in soundregs[16 * chip]

  // MAME mix: AY#0 0.15, AY#1 0.5, AY#2 0.5, AY#3 1.0 (AY#0 rounded up)
  static const uint8_t AY_INC = 8;   // 1.5 MHz / 8 / 24 kHz
  static const uint8_t AY3_GAIN = 4;
  Ay8910 ay[4] = {Ay8910(AY_INC, 1), Ay8910(AY_INC, AY3_GAIN / 2),
                  Ay8910(AY_INC, AY3_GAIN / 2), Ay8910(AY_INC, AY3_GAIN)};

  // 8 bit DAC on AY#1 port A, volume ladder on port B. The sound CPU
  // writes it at any time: sampled at 24 kHz in run_frame(), popped per
  // output sample in renderAudio() (circusc pattern)
  static const uint16_t DAC_RING = 1024;   // 2.5 frames
  volatile int16_t dac_ring[DAC_RING] = {};
  volatile uint16_t dac_wr = 0, dac_rd = 0;
  int16_t dac_last = 0;     // audio core
  uint16_t dac_acc = 0;
  int16_t dac_sample = 0;   // current DAC output, emulation core

  void updateDac(void);
  void pushDac(void);
};

#endif
