#ifndef JAILBREK_H
#define JAILBREK_H

#include "../machineBase.h"
#include "../../catalog.h"
#if defined(GAME_JAILBREK)

#include "../../emulation/sn76489.h"
#include "../../emulation/vlm5030.h"
#include "../../emulation/speech_ring.h"
#include "../../emulation/seqlock.h"
#include "../../cpus/m6809/m6809.h"
#include "jailbrek_arena_plan.h"
#include "jailbrek_dipswitches.h"

// Jail Break (Konami 1986, GX507), MAME konami/jailbrek.cpp + k005849.cpp.
// Konami-1 6809 @ 1.536 MHz, SN76489A @ 1.536 MHz, VLM5030 @ 3.58 MHz.
// ROT0 256x224, shown at 240x224: native x 8..247 -> column x - 8, native
// y 16..239 -> galagino row y - 16 + 32 (gberet geometry).

// memory[] = CPU address for 0000-1fff, K005849 scroll RAM appended
#define JB_MEM_CRAM   0x0000  // 0000-07ff color RAM
#define JB_MEM_VRAM   0x0800  // 0800-0fff video RAM, 64x32
#define JB_MEM_SPR    0x1000  // 1000-10ff sprites, ctrl3 b3 = 0
#define JB_MEM_SPR2   0x1100  // 1100-11ff sprites, ctrl3 b3 = 1
#define JB_MEM_SCROLL 0x2000  // 2000-203f row/column scroll, low byte / bit 8
#define JB_MEM_END    0x2040
static_assert(JB_MEM_END <= RAMSIZE, "jailbrek RAM does not fit");

class jailbrek : public machineBase {
protected:
  jailbrek_spritemap_asset jailbrek_spritemap;
  jailbrek_rom_asset jailbrek_rom;
  jailbrek_tilemap_asset jailbrek_tilemap;
  jailbrek_vlm_asset jailbrek_vlm;

public:
  jailbrek();

  uint8_t machineType() override { return MCH_JAILBREK; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

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

  static const int CYCLES_PER_LINE = 96;  // 384 pixel clocks at 6.144 MHz
  static const int LINES = 264;

private:
  static const int OUT_WIDTH = 240;
  static const int VIS_LINES = 224;
  static const int FIRST_TILE_ROW = 2;  // native y 16
  static const int VIS_TILE_ROWS = 28;
  static const int VIS_TILES = VIS_TILE_ROWS * 64;
  static const int SPRITE_BYTES = 0xc0; // 48 sprites x 4 bytes

  // Render input, latched at vblank (see seqlock.h). Visible tile rows only;
  // column scroll can show rows 0-1 and 30-31, read live (credits only).
  struct VideoState {
    uint8_t cram[VIS_TILES];
    uint8_t vram[VIS_TILES];
    uint8_t spr[SPRITE_BYTES];  // bank selected at vblank
    uint8_t scroll[0x40];
    uint8_t ctrl[8];
  };
  Seqlock<VideoState> vblank;
  VideoState video;  // render copy, video core only

  void ctrl_w(uint8_t reg, uint8_t v);
  void publish_video();
  bool main_idle();
  bool in_idle_loop() const;
  void vlm_catch_up();
  uint8_t read_io(uint16_t addr);
  void render_tiles_rows(unsigned short *dst, int line);
  void render_tiles_cols(unsigned short *dst, int line);
  void render_line(unsigned short *dst, int line);

#ifdef LED_PIN
  uint8_t led_frame = 0;  // gameLeds() blink counter
#endif

  m6809_state main_cpu;
  uint32_t main_cycles = 0;  // 6809 cycles since reset
  uint8_t ctrl[8] = {};      // K005849 control registers 2040-2047
  bool nmi_line = false, irq_line = false;  // held until the enable bit clears

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

#endif // GAME_JAILBREK
#endif // JAILBREK_H
