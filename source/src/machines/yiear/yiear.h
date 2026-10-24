#ifndef YIEAR_H
#define YIEAR_H

#include "../machineBase.h"
#include "../../emulation/sn76489.h"
#include "../../emulation/vlm5030.h"
#include "../../emulation/speech_ring.h"
#include "../../emulation/seqlock.h"
#include "../../cpus/m6809/m6809.h"
#include "yiear_dipswitches.h"
#include "yiear_logo.h"

// Yie Ar Kung-Fu (Konami 1985, version I), MAME konami/yiear.cpp.
// 6809 @ 1.536 MHz, SN76489A @ 1.536 MHz, VLM5030 speech @ 3.58 MHz.
// ROT0 256x224, shown at 240x224: native x 8..247 -> column x - 8, native
// y 16..239 -> galagino row y - 16 + 32.

// memory[] = CPU address - 0x5000
#define YIEAR_MEM_SPR0 0x0000  // 5000-502f sprite attr / y
#define YIEAR_MEM_SPR1 0x0400  // 5400-542f sprite x / code
#define YIEAR_MEM_VRAM 0x0800  // 5800-5fff tilemap, 2 bytes per tile
#define YIEAR_MEM_END  0x1000
static_assert(YIEAR_MEM_END <= RAMSIZE, "yiear RAM does not fit");

class yiear : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> yiear_rom;
  Asset<unsigned char, COMPRESSED> yiear_vlm;
  Asset<uint32_t[8], COMPRESSED> yiear_tilemap;
  Asset<uint32_t[16][2], COMPRESSED> yiear_spritemap;

public:
  yiear();

  uint8_t machineType() override { return MCH_YIEAR; }
  void renderAudio(int16_t *out, int n) override;
  const int renderWidth() override { return OUT_WIDTH; }
  const int renderBuffer() override { return OUT_WIDTH * 2 * 8; }

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  void reset() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void) { return yiear_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const int OUT_WIDTH = 240;
  static const int VIS_LINES = 224;
  static const int FIRST_TILE_ROW = 2;  // native y 16
  static const int VIS_TILE_ROWS = 28;
  static const int SPRITE_BYTES = 0x30; // 24 sprites x 2 bytes per bank

  // Render input, latched at vblank (see seqlock.h). Visible tile rows only.
  struct VideoState {
    uint8_t vram[VIS_TILE_ROWS * 64];
    uint8_t spr0[SPRITE_BYTES];
    uint8_t spr1[SPRITE_BYTES];
  };
  Seqlock<VideoState> vblank;
  VideoState video;  // render copy, video core only

  void control_w(uint8_t v);
  void publish_video();
  bool main_idle();
  bool in_idle_loop() const;
  void vlm_catch_up();
  uint8_t read_io(uint16_t addr);
  void render_line(unsigned short *dst, int line);

  m6809_state main_cpu;
  uint32_t main_cycles = 0;  // 6809 cycles since reset
  bool nmi_enable = false, irq_enable = false;
  bool nmi_line = false, irq_line = false;  // held until the enable bit clears
  uint8_t sn_latch = 0;

  // VLM5030: one sample per 440 chip clocks, run up to main_cycles
  Vlm5030 vlm;
  uint32_t vlm_next = 0;  // main cycle of the next VLM sample
  SpeechRing speech;      // vlm -> audio core

  // Cached: hot path reads these per access.
  const unsigned char *rom_ptr = nullptr;
  const unsigned char *vlm_rom = nullptr;
  const uint32_t (*tiles)[8] = nullptr;
  const uint32_t (*sprites)[16][2] = nullptr;

protected:
  Sn76489 sn[1] = {Sn76489(Sn76489::INC_1536KHZ, Sn76489::HOLD_SHORT_NOTES)};
};

#endif
