#ifndef PHOZON_H
#define PHOZON_H

// ============================================================================
// Phozon (Namco 1983), Super Pacman class board with a third CPU:
// 3x MC6809E @ 1.536 MHz (main, sound, sub), WSG 15XX 8 voices, Namco
// 58XX (inputs) + 56XX (DIPs), static 36x28 tilemap with 512 chars,
// 64 sprites built from 8x8 2bpp cells (8, 16 or 32 px per axis).
//
// MAME: namco/mappy.cpp (phozon_cpu*_map, mappy_state::phozon),
// namco/mappy_v.cpp (phozon_draw_sprites, screen_update_phozon).
// Gfx pre-rotated ROT90 by romconv/internal/phozon/phozon_rom_convert.py:
//   galagino row (0..287)    == MAME x
//   galagino column (0..223) == 223 - MAME y
// ============================================================================

#include "../machineBase.h"
#include "../../emulation/namco_wsg.h"
#include "../../emulation/seqlock.h"
#include "../namcoio.h"
#include "../tileaddr.h"
#include "../../cpus/m6809/m6809.h"
#include "phozon_rom_main.h"
#include "phozon_rom_sound.h"
#include "phozon_rom_sub.h"
#include "phozon_tilemap.h"
#include "phozon_spritemap.h"
#include "phozon_cmap.h"
#include "phozon_wavetable.h"
#include "phozon_dipswitches.h"
#include "phozon_logo.h"

// CPU interleave, same as gaplus (3x 6809, 100 slices/frame in MAME)
#define PHOZON_SLICES 1600

// memory[] layout:
//   0x0000-0x1fff  videoram (tiles 0x000, attr 0x400) + work/sprite RAM,
//                  shared by main and sub CPU at the same addresses
//   0x2000-0x23ff  15XX shared RAM (0x4000 main/sub, 0x0000 sound CPU);
//                  0x00-0x3f are the WSG registers, kept in soundregs[]
//   0x2400-0x2bff  sub CPU private RAM (0xa000-0xa7ff)
#define PHOZON_SHARED_OFF   0x2000
#define PHOZON_SUB_RAM_OFF  0x2400
#define PHOZON_SUB_RAM_SIZE 0x0800

class phozon : public machineBase
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> phozon_rom_main;
  Asset<unsigned char, COMPRESSED> phozon_rom_sound;
  Asset<unsigned char, COMPRESSED> phozon_rom_sub;
  Asset<unsigned short[8], COMPRESSED> phozon_tilemap;
  Asset<unsigned short[8], COMPRESSED> phozon_sprites;

public:
  phozon();

  signed char machineType() override { return MCH_PHOZON; }
  signed char useVideoHalfRate() override { return 1; }

  void reset() override;

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;

  void renderAudio(int16_t *out, int n) override {
    wsg.render(soundregs, phozon_wavetable, wsg_enable != 0, out, n);
    audioShiftRight(out, n, 1);
  }

  static const Logo &logo(void) { return phozon_logo; }

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  enum TilePass { TILES_ALL, TILES_PRIO };

  void render_tiles(short row, TilePass pass);
  void blit_tile(short row, uint8_t col, TilePass pass);
  void render_sprites(short row);
  void blit_cell(short row, uint16_t code, uint8_t color, short x, short y);

  uint8_t shared_r(uint16_t off);
  void shared_w(uint16_t off, uint8_t val);
  uint8_t main_r(uint16_t addr);
  void main_w(uint16_t addr, uint8_t val);
  uint8_t sub_r(uint16_t addr);
  void sub_w(uint16_t addr, uint8_t val);
  void mainlatch_w(uint16_t addr);
  void install_rom_direct(void);
  uint8_t io_in(uint8_t chip, uint8_t port);

  m6809_state main_cpu;
  m6809_state sound_cpu;   // MAME "sub": 15XX only
  m6809_state sub_cpu;     // MAME "sub2": shares video/sprite RAM with main

  const unsigned char *rom_main;              // 32KB, main 0x8000-0xffff
  const unsigned char *rom_sound;             // 8KB,  sound 0xe000-0xffff
  const unsigned char *rom_sub;               // 8KB,  sub 0xe000-0xffff
  const unsigned short (*tiles)[8];           // 512 chars
  const unsigned short (*sprites)[8];         // 512 cells x 4 flip variants

  NamcoIO io[2];               // 0 = 58XX inputs, 1 = 56XX DIPs
  uint8_t dipmux_sel;          // LS157 select from 56XX pin 13

  // mainlatch LS259 @0x5000 (A0 = data), MAME mappy_state::phozon:
  // Q0 sound IRQ mask, Q1 main IRQ mask, Q2 sub IRQ mask, Q3 sound enable,
  // Q4 !reset namcoio, Q5 !reset sound CPU, Q6 !reset sub CPU
  uint8_t main_irq_mask, sound_irq_mask, sub_irq_mask;
  uint8_t wsg_enable;
  uint8_t io_reset, sound_reset, sub_reset;

  // Sprite tables latched at vblank (Seqlock), so one rendered frame never
  // mixes two emulated ones. The tilemap has no scroll and is read live,
  // like gaplus. 3 x 128 B, the three sprite RAM banks MAME reads.
  static const uint16_t SPRITE_TABLE = 0x80;
  struct VideoState {
    uint8_t s1[SPRITE_TABLE];   // 0x0f80: code, color
    uint8_t s2[SPRITE_TABLE];   // 0x1780: y, x
    uint8_t s3[SPRITE_TABLE];   // 0x1f80: flags, enable/x msb
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  // one MAME sprite entry of 1-4 x 1-4 cells, galagino coordinates:
  // cell (cx, cy) top-left at row y + 8*cx, column x - 8*cy
  struct Block {
    int16_t x, y;
    uint16_t code;
    uint8_t color;
    uint8_t flipx, flipy;
    uint8_t sizex, sizey;       // cells - 1 along MAME x / y
  };
  static const uint8_t MAX_BLOCKS = SPRITE_TABLE / 2;
  Block blocks[MAX_BLOCKS];
  uint8_t n_blocks;

private:
  Namco15xx wsg;
};

#endif
