#ifndef SCOTRSHT_H
#define SCOTRSHT_H

#include "../machineBase.h"
#include "../../catalog.h"
#if defined(GAME_SCOTRSHT)

#include "../../emulation/ym2203.h"
#include "../../emulation/ym2203fm.h"
#include "../../emulation/seqlock.h"
#include "../../cpus/m6809/m6809.h"
#include "scotrsht_arena_plan.h"
#include "scotrsht_dipswitches.h"

// Scooter Shooter (Konami 1985, GX545), MAME konami/scotrsht.cpp.
// MC6809E 3.072 MHz main, Z80 3.072 MHz sound with one YM2203 (same clock).
// 64x32 tilemap, 32 visible columns each with its own vertical scroll,
// 48 sprites, runtime palette bank (8 x 16 colors).
// ROT90 onto the panel like circusc: native column tx -> strip tx + 2,
// native y -> panel x 239 - y.

// memory[]: CPU address for 0000-201f, sound RAM after it
#define SC_MEM_CRAM   0x0000  // 0000-07ff color RAM, 64x32
#define SC_MEM_VRAM   0x0800  // 0800-0fff video RAM, 64x32
#define SC_MEM_SPR    0x1000  // 1000-10bf sprites, 48 x 4
#define SC_MEM_SCROLL 0x2000  // 2000-201f column scroll
#define SC_MEM_END    0x2020
#define SC_MEM_SND    0x2400  // sound CPU 4000-43ff
#define SC_SND_SIZE   0x0400
static_assert(SC_MEM_SND + SC_SND_SIZE <= RAMSIZE, "scotrsht RAM does not fit");

class scotrsht : public machineBase {
protected:
  scotrsht_spritemap_asset scotrsht_spritemap;
  scotrsht_rom_asset scotrsht_rom;
  scotrsht_tilemap_asset scotrsht_tilemap;
  scotrsht_sound_asset scotrsht_sound;

public:
  scotrsht();

  signed char machineType() override { return MCH_SCOTRSHT; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

  void renderAudio(int16_t *out, int n) override;

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char inZ80(unsigned short Port) override;
  void outZ80(unsigned short Port, unsigned char Value) override;

  void reset() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;

  static const int CYCLES_PER_FRAME = 51200;  // 3.072 MHz / 60, both CPUs
  static const int SLICES = 256;
  static const int SLICE_CYCLES = CYCLES_PER_FRAME / SLICES;

private:
  static const int VIS_COLS = 32;   // native tile columns 0-31 of 64
  static const int TILE_ROWS = 32;
  static const int SPRITE_BYTES = 0xc0;

  // Render input, latched at vblank (see seqlock.h). Tile RAM goes with
  // the scroll: every row can scroll into view.
  struct VideoState {
    uint8_t cram[TILE_ROWS * VIS_COLS];
    uint8_t vram[TILE_ROWS * VIS_COLS];
    uint8_t scroll[VIS_COLS];
    uint8_t spr[SPRITE_BYTES];
    uint8_t charbank;
    uint8_t palbank;
  };
  Seqlock<VideoState> vblank;
  VideoState video;  // render copy, video core only

  void publish_video();
  void run_main(int cycles);
  bool main_idle();
  bool in_main_loop() const;
  void run_sound(int cycles);
  bool sound_idle() const;
  int step_sound();
  void build_cmaps(uint8_t bank);
  void blit_tile(unsigned short addr, short x);
  void blit_spr(short row, unsigned char s);

  m6809_state main_cpu;
  int main_debt = 0;
  int snd_debt = 0;
  bool irq_enable = false;
  bool irq_line = false;  // vblank IRQ held until taken (HOLD_LINE)
  uint8_t charbank = 0;
  uint8_t palbank = 0;
  uint8_t soundlatch = 0;
  bool latch_pending = false;  // drives the Z80 IRQ until 8000 is read
#ifdef LED_PIN
  uint8_t led_frame = 0;  // gameLeds() blink counter
#endif
  bool idle_ok = false;        // sound idle loop found in the ROM

  // video core: colors of the current palette bank
  uint8_t cmap_bank = 0xff;
  uint16_t tile_cmap[16][16];
  uint16_t spr_cmap[16][16];
  uint16_t spr_transp[16];  // pen bit set: lookup nibble 0

  // Cached: hot path reads these per access.
  const unsigned char *rom_ptr = nullptr;
  const unsigned char *sound_ptr = nullptr;
  const unsigned char (*tiles)[8][4] = nullptr;
  const unsigned char (*sprites)[16][8] = nullptr;

  static const uint8_t AY_INC = 4;   // SSG 3.072 MHz / 4 = 768 kHz
  static const uint8_t AY_GAIN = 5;
  Ym2203Fm fm{3072000};
  Ym2203 ym = Ym2203(AY_INC, AY_GAIN, &fm);  // after fm, which it points to
};

#endif // GAME_SCOTRSHT
#endif // SCOTRSHT_H
