#ifndef TRACKFLD_H
#define TRACKFLD_H

#include "../machineBase.h"
#include "../../emulation/sn76489.h"
#include "../../emulation/vlm5030.h"
#include "../../emulation/speech_ring.h"
#include "../../emulation/dc_block.h"
#include "../../emulation/seqlock.h"
#include "../../cpus/m6809/m6809.h"
#include "trackfld_dipswitches.h"
#include "trackfld_logo.h"

// Track & Field (Konami 1983), MAME konami/trackfld.cpp.
// KONAMI-1 6809 @ 1.536 MHz; sound board Z80 @ 3.58 MHz with SN76489A,
// 8-bit DAC and VLM5030. ROT0 256x224, shown at 240x224: native x 8..247
// -> column x - 8, native y 16..239 -> galagino row y - 16 + 32.

// memory[] offsets
#define TF_MEM_RAM   0x0000  // main 1800-1fff
#define TF_MEM_SPR2  0x0000  //   1800-183f sprite attr / y
#define TF_MEM_SCRL  0x0040  //   1840-185f row scroll low byte
#define TF_MEM_SPR   0x0400  //   1c00-1c3f sprite x / code
#define TF_MEM_SCRH  0x0440  //   1c40-1c5f row scroll bit 8
#define TF_MEM_NVRAM 0x0800  // main 2800-2fff (world records, not persisted)
#define TF_MEM_VRAM  0x1000  // main 3000-37ff
#define TF_MEM_CRAM  0x1800  // main 3800-3fff
#define TF_MEM_SND   0x2000  // Z80 4000-43ff
#define TF_MEM_END   0x2400
static_assert(TF_MEM_END <= RAMSIZE, "trackfld RAM does not fit");

class trackfld : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> trackfld_rom_main;
  Asset<unsigned char, COMPRESSED> trackfld_rom_sound;
  Asset<unsigned char, COMPRESSED> trackfld_vlm;
  Asset<uint32_t[8], COMPRESSED> trackfld_tilemap;
  Asset<uint32_t[16][2], COMPRESSED> trackfld_spritemap;

public:
  trackfld();

  signed char machineType() override { return MCH_TRACKFLD; }
  void renderAudio(int16_t *out, int n) override;
  const int renderWidth() override { return OUT_WIDTH; }
  const int renderBuffer() override { return OUT_WIDTH * 2 * 8; }

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  void reset() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void) { return trackfld_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const int OUT_WIDTH = 240;
  static const int VIS_LINES = 224;
  static const int FIRST_TILE_ROW = 2;  // native y 16
  static const int VIS_TILE_ROWS = 28;
  static const int SPRITE_BYTES = 0x40; // 32 sprites x 2 bytes per bank

  // Render input, latched at vblank (see seqlock.h). Visible tile rows only.
  struct VideoState {
    uint8_t vram[VIS_TILE_ROWS * 64];
    uint8_t cram[VIS_TILE_ROWS * 64];
    uint16_t scroll[VIS_TILE_ROWS];
    uint8_t spr[SPRITE_BYTES];
    uint8_t spr2[SPRITE_BYTES];
  };
  Seqlock<VideoState> vblank;
  VideoState video;  // render copy, video core only

  void latch_w(uint8_t bit, bool q);
  void publish_video();
  bool main_idle();
  bool in_idle_loop() const;
  void vlm_catch_up();
  void dac_tick();
  uint8_t read_io(uint16_t addr);
  void add_sprite(int x, int y, uint8_t code, uint8_t attr);
  void render_line(unsigned short *dst, int line);

  m6809_state main_cpu;
  uint32_t main_cycles = 0;  // 6809 cycles since reset, timeline for VLM and timer
  bool irq_mask = false;     // LS259 Q7, holds the vblank IRQ until cleared
  bool irq_line = false;
  bool sound_q = false;      // LS259 Q1, Z80 IRQ on its rising edge
  bool snd_irq = false;      // Z80 IRQ held until taken (HOLD_LINE)
  uint8_t soundlatch = 0, sn_latch = 0;

  // DAC: sampled at 24 kHz on the emulation core, DC blocked on render
  uint8_t dac = 0x80;
  uint16_t dac_acc = 0;
  SpeechRing dac_ring;
  DcBlock dac_dc;

  // VLM5030 on the sound board: one sample per 440 chip clocks
  Vlm5030 vlm;
  uint32_t vlm_next = 0;
  SpeechRing speech;

  // Cached: hot path reads these per access.
  const unsigned char *main_rom = nullptr;
  const unsigned char *sound_rom = nullptr;
  const unsigned char *vlm_rom = nullptr;
  const uint32_t (*tiles)[8] = nullptr;
  const uint32_t (*sprites)[16][2] = nullptr;

protected:
  Sn76489 sn[1] = {Sn76489(Sn76489::INC_1789KHZ, Sn76489::HOLD_SHORT_NOTES)};
};

#endif
