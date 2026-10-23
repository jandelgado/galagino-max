#ifndef MEGAZONE_H
#define MEGAZONE_H

#include "../machineBase.h"
#include "../../emulation/ay8910.h"
#include "../../emulation/i8039_dac.h"
#include "../../emulation/seqlock.h"
#include "../../cpus/m6809/m6809.h"
#include "megazone_dipswitches.h"
#include "megazone_logo.h"

// Mega Zone (Konami 1983), MAME konami/megazone.cpp, set megazone.
//
//   main KONAMI-1 1.536 MHz --shared RAM 3800-3fff--> sound Z80 3.072 MHz
//     tiles, sprites, scroll        (e000-e7ff)        inputs, DIPs, AY-3-8910
//                                                      |  latch 4000, INT 2000
//                                                      v
//                                              i8039 7.16 MHz -> P1 8-bit DAC
//
// Video ROT90: native (X, Y), X 0..287, visible Y 16..239 -> panel row X
// (all 36 strips), column 239 - Y. Native X 0..47: fixed score panel from
// the second tilemap, drawn over everything.

// memory[] offsets (= main address - 0x2000)
#define MEGAZONE_VRAM0     0x0000   // 2000-23ff playfield codes
#define MEGAZONE_VRAM1     0x0400   // 2400-27ff panel codes
#define MEGAZONE_CRAM0     0x0800   // 2800-2bff playfield attributes
#define MEGAZONE_CRAM1     0x0c00   // 2c00-2fff panel attributes
#define MEGAZONE_SPRITES   0x1000   // 3000-33ff
#define MEGAZONE_SHARED    0x1800   // 3800-3fff, sound e000-e7ff
#define MEGAZONE_RAM_END   0x2000
static_assert(MEGAZONE_RAM_END <= RAMSIZE, "megazone RAM exceeds memory[]");

class megazone : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> megazone_rom_main;
  Asset<unsigned char, COMPRESSED> megazone_rom_sound;
  Asset<unsigned char, COMPRESSED> megazone_rom_i8039;
  Asset<uint32_t[8], COMPRESSED> megazone_chars;
  Asset<unsigned char[16][8], COMPRESSED> megazone_sprites;
  Asset<uint16_t, COMPRESSED> megazone_rgb;
  Asset<unsigned char, COMPRESSED> megazone_lut;

public:
  megazone();
  signed char machineType() override { return MCH_MEGAZONE; }
  void renderAudio(int16_t *out, int n) override;
  void reset() override;
  void start() override;

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char inZ80(unsigned short Port) override;
  void outZ80(unsigned short Port, unsigned char Value) override;

  void wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char val) override;
  unsigned char rdI8048_port(struct i8048_state_S *state, unsigned char port) override;
  unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) override;
  unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
  static const Logo &logo(void) { return megazone_logo; }

private:
  static const int PANEL_COLS = 6;      // native X 0..47
  static const int SPRITE_BYTES = 0x400;

  // Render input latched at vblank (seqlock.h): playfield, the 6 panel
  // columns, sprites, scroll
  struct VideoState {
    uint8_t vram0[0x400], cram0[0x400];
    uint8_t vram1[32 * PANEL_COLS], cram1[32 * PANEL_COLS];
    uint8_t sprites[SPRITE_BYTES];
    uint8_t scroll_x;   // 0x1800: native Y scroll
    uint8_t scroll_y;   // 0x1000: native X scroll
  };

  uint8_t soundRead(uint16_t addr);
  uint8_t ayPortA(void);
  void publishVideo(void);
  void drawTileColumn(const uint8_t *vram, const uint8_t *cram, uint8_t stride, uint8_t tx,
                      uint8_t nx, uint8_t sy, uint16_t *dst);
  void drawSprite(short row, const uint8_t *s);

  m6809_state main_cpu;
  I8039Dac dac{7159090 / 15, 2};   // (P1 - 128) * 2 next to the AY
  Seqlock<VideoState> vblank;
  VideoState video = {};

  uint8_t irq_mask = 0;
  uint8_t scroll_x = 0, scroll_y = 0;
  uint8_t sound_latch = 0;
  uint8_t ay_addr = 0;
  uint8_t i8039_status = 0;   // P2 b4-6 -> AY port A b0-2
  uint32_t snd_cycles = 0;    // Z80 cycles, for the AY port A timer

  const uint8_t *main_rom = nullptr, *sound_rom = nullptr, *i8039_rom = nullptr;
  const uint32_t (*chars)[8] = nullptr;
  const uint8_t (*sprite_gfx)[16][8] = nullptr;

  uint16_t cmap_chars[16][16];     // RGB565 per char color and pen
  uint16_t cmap_sprites[16][16];
  uint16_t sprite_opaque[16];      // bit p: sprite pen p drawn (lookup != 0)
  uint16_t visible[256];           // sprite offsets on screen, draw order
  int visible_count = 0;

  static const uint8_t AY_INC = 9;   // 1.789772 MHz / 8 / 24 kHz
  static const uint8_t AY_GAIN = 6;
  Ay8910 ay[1] = {Ay8910(AY_INC, AY_GAIN)};
};

#endif
