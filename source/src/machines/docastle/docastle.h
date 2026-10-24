#ifndef DOCASTLE_H
#define DOCASTLE_H

#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/sn76489.h"
#include "docastle_dipswitches.h"
#include "docastle_logo.h"
#include "docastle_arena_plan.h"

// Mr. Do's Castle (Universal 1983), MAME 0.289 universal/docastle.cpp:
// main and slave Z80 at 4 MHz talking through a one byte latch, slave reads
// the inputs (TMS1025 mux) and drives 4 SN76489A. The third Z80 only
// forwards sprite RAM to the sprite chip; like MAME it is not emulated.
//
// Screen 240x192 ROT270: native (x 8-247, y 0-191) -> panel row
// 24 + 247 - x, column 16 + y (192 of 224 columns, 240 of 288 rows).

// memory[] offsets
#define DOCASTLE_MEM_MAIN_RAM  0x0000  // 8000-97ff
#define DOCASTLE_MEM_SPRITES   0x1800  // 9800-99ff
#define DOCASTLE_MEM_VIDEO     0x1a00  // b000-b3ff
#define DOCASTLE_MEM_COLOR     0x1e00  // b400-b7ff
#define DOCASTLE_MEM_SLAVE_RAM 0x2200  // slave 8000-87ff
#define DOCASTLE_MEM_END       0x2a00
static_assert(DOCASTLE_MEM_END <= RAMSIZE, "docastle RAM does not fit");

class docastle : public machineBase {
protected:
  docastle_main_rom_asset docastle_main_rom;
  docastle_slave_rom_asset docastle_slave_rom;
  docastle_tiles_asset docastle_tiles;
  docastle_sprites_asset docastle_sprites;

public:
  docastle();

  uint8_t machineType() override { return MCH_DOCASTLE; }

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  void reset() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  void renderAudio(int16_t *out, int n) override;
  static const Logo &logo(void) { return docastle_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const int OUT_WIDTH = 224;
  static const int TILE_BYTES = 0x400;
  static const int SPRITE_BYTES = 0x200;

  // Render input, latched right before the main CPU's vblank IRQ (seqlock.h).
  struct VideoState {
    uint8_t video[TILE_BYTES];
    uint8_t color[TILE_BYTES];
    uint8_t sprites[SPRITE_BYTES];
  };
  Seqlock<VideoState> vblank;
  VideoState video;  // render copy, video core only

  int stepMain();
  int stepSlave();
  bool mainIdle();
  bool slaveIdle();
  bool latchReadBlocks();
  uint8_t readInputs(uint8_t offset);
  uint8_t inputPort(uint8_t select);
  void renderTiles(int strip);
  void renderSprites(int strip);

  // Latch between the CPUs. A main write holds main in WAIT until the slave
  // reads it; a main read waits for a slave write since main's last access.
  uint8_t latch = 0;
  bool main_wait = false;
  bool latch_fresh = false;  // slave wrote, main has not read it yet
  bool slave_nmi = false;
  bool main_irq = false;
  bool slave_irq = false;
  int sn_wait = 0;         // slave cycles lost to SN76489A READY this step
  int main_carry = 0;      // cycles run past the end of the last frame
  int slave_carry = 0;

  // TMS1025 pair: select latched after each read, data from the previous one
  uint8_t mux_select = 0;
  uint8_t mux_data = 0;

  // strip buffers, video core only
  uint8_t fg[8][192];       // tile pixel with pen 8-15: drawn over sprites
  uint8_t covered[8][192];  // MAME priority bitmap: pixel taken by a sprite

  // Cached: hot path reads these per access.
  const unsigned char *main_rom = nullptr;
  const unsigned char *slave_rom = nullptr;
  const uint32_t (*tiles)[8] = nullptr;
  const uint32_t (*sprite_gfx)[16][2] = nullptr;

  Sn76489 sn[4] = {Sn76489(Sn76489::INC_4MHZ), Sn76489(Sn76489::INC_4MHZ),
                   Sn76489(Sn76489::INC_4MHZ), Sn76489(Sn76489::INC_4MHZ)};
};

#endif
