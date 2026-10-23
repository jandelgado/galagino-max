#ifndef CONTRA_H
#define CONTRA_H

#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/spriteflip.h"
#include "../../emulation/sidplayer.h"
#include "contra_dipswitches.h"
#include "contra_sound.h"
#include "contra_logo.h"

// Contra (Konami 1987), MAME konami/contra.cpp, set contra.
//
//   HD6309 3 MHz (run as 6809: the 68B09 bootleg runs the same ROM)
//     |  0000-00bf  K007121 #0/#1 registers, 007452 math, inputs, latch
//     |  0c00-5fff  palette, RAM, tile/sprite RAM of both K007121
//     |  6000-7fff  16 x 8 KB ROM banks, 8000-ffff fixed ROM
//     +--latch 001c--> sound 6809 + YM2151: not emulated; the commands
//                      cue Gryzor.sid (contra_sound.h) on the audio core
//
// The CPU sees ram[] for 0000-5fff (addresses = offsets).
//
// Video ROT90: native (X, Y), X 0..279, visible Y 16..239 -> panel line
// X + 4 (280 of 288 lines), column 239 - Y. Native X 0..39: fixed text
// layer (HUD), X 40..279: two scrolling K007121 layers and sprites.

// ram[] offsets (= main address) of the K007121 tilemaps and sprites
#define CONTRA_PALETTE   0x0c00   // 128 x xBGR555 little endian
#define CONTRA_CRAM0     0x2000   // layer 0 (K007121 #0) attributes
#define CONTRA_VRAM0     0x2400   // layer 0 codes
#define CONTRA_CRAM2     0x2800   // text layer (#0 gfx, fixed)
#define CONTRA_VRAM2     0x2c00
#define CONTRA_SPRITES0  0x3000   // 2 x 0x800 sprite list banks of #0
#define CONTRA_CRAM1     0x4000   // layer 1 (K007121 #1)
#define CONTRA_VRAM1     0x4400
#define CONTRA_SPRITES1  0x5000
#define CONTRA_RAM_END   0x6000

class contra : public machineBase {
protected:
  Asset<unsigned char, PLAIN> contra_main_rom;
  Asset<unsigned char, COMPRESSED> contra_lut;
  Asset<uint32_t[8], PLAIN> contra_tiles;
  Asset<unsigned short, PLAIN> contra_tile_index;
  Asset<unsigned char, PLAIN> contra_sid;

public:
  contra();
  signed char machineType() override { return MCH_CONTRA; }
  void reset() override;
  void renderAudio(int16_t *out, int n) override;

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
  static const Logo &logo(void) { return contra_logo; }

private:
  static const int CHIPS = 2;
  static const int CTRL_REGS = 8;
  static const int MATH_REGS = 6;
  static const int LAYERS = 3;              // 0, 1: scrolling (#0, #1), 2: text (#0)
  static const int MAP_BYTES = 0x400;       // 32 x 32 tiles
  static const int SPRITE_LIST = 0x800;     // one bank of a K007121 sprite RAM
  static const int COLORS = 128;            // per K007121, 16 pens each
  static const int PENS = 16;
  static const int MAX_BLOCKS = 264;        // 8x8 sprite blocks per K007121 list
  static const int SPRITE_BYTES = 5;

  // one visible 8x8 sprite block, in draw order
  struct SpriteBlock {
    int16_t x;        // native X of its first column
    int16_t col;      // panel column of its native bottom row
    uint16_t tile;    // contra_tiles index
    uint8_t color;    // pens[chip] color
    uint8_t flags;    // chip, flips
  };

  // Render input latched at vblank (seqlock.h): palette, tilemaps, the
  // selected sprite list banks, control registers
  struct VideoState {
    uint8_t palette[0x100];
    uint8_t cram[LAYERS][MAP_BYTES], vram[LAYERS][MAP_BYTES];
    uint8_t sprites[CHIPS][SPRITE_LIST];
    uint8_t ctrl[CHIPS][CTRL_REGS];
  };

  uint8_t readPort(uint16_t addr);
  uint8_t readSystem(unsigned int keys);
  uint8_t readPlayer1(unsigned int keys);
  void cue(uint8_t cmd);
  bool idle(void);
  void writeCtrl(int chip, uint8_t reg, uint8_t val);
  void writeMath(uint8_t reg, uint8_t val);
  uint8_t readMath(uint8_t reg);
  void publishVideo(void);
  void latchSprites(void);
  void buildPens(void);
  uint16_t tileCode(int layer, int index, uint8_t &color) const;
  void drawLayer(int layer, int x, uint16_t *dst, bool opaque) const;
  void listSprites(int chip);
  void drawSprites(short row);

  m6809_state main_cpu;
  const uint8_t *main_rom = nullptr;
  uint8_t *ram = nullptr;              // Arena, CONTRA_RAM_END bytes
  const uint8_t *lut = nullptr;        // 4 x 256 color lookup PROMs
  const uint32_t (*tiles)[8] = nullptr;
  const uint16_t *tile_index = nullptr;

  Seqlock<VideoState> *vblank = nullptr;   // Arena
  uint8_t (*sprite_latch)[SPRITE_LIST] = nullptr;   // Arena, [chip], drawn next frame
  VideoState *video = nullptr;             // Arena, render copy
  uint16_t (*pens)[COLORS][PENS] = nullptr;  // Arena, [chip], RGB565 swapped
  uint16_t opaque[CHIPS][COLORS];            // bit p: sprite pen p drawn (lookup != 0)
  uint8_t pens_palette[0x100];               // palette RAM pens were built from
  bool pens_valid = false;
  SpriteBlock *blocks = nullptr;             // Arena, CHIPS * (MAX_BLOCKS + 15)
  int block_count = 0;

  uint8_t ctrl[CHIPS][CTRL_REGS] = {};  // K007121 control registers
  uint8_t bank = 0;                     // 6000-7fff ROM bank
  bool irq_line = false;                // K007121 #0 vblank IRQ, level

  // sound commands: pushed by the emulation core, cued in renderAudio()
  static const uint8_t SND_RING = 8;
  static const uint16_t TUNE_RAM_SIZE = 0x1000;   // C64 $03EC-$13EB
  volatile uint8_t snd_ring[SND_RING] = {};
  volatile uint8_t snd_wr = 0, snd_rd = 0;
  PsidInfo tune;
  SidPlayer player;
  bool tune_ok = false;
  bool sid_on = false;     // player renders
  bool music_on = false;   // a song runs: effects go through its driver

  // 007452: w0 * w1 -> r0/r1; w4w5 / w2w3 -> r4/r5 quotient, r2/r3 remainder
  uint8_t math_regs[MATH_REGS] = {};
  uint16_t product = 0, quotient = 0, remainder = 0;
};

#endif
