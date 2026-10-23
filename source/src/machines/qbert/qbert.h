#ifndef QBERT_H
#define QBERT_H

#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/dc_block.h"
#include "../gottlieb/gottlieb_sound.h"
#include "../../cpus/i8086/i8086.h"
#include "../gottlieb/gottlieb_video.h"
#include "qbert_dipswitches.h"
#include "qbert_logo.h"

// Q*bert (Gottlieb 1982), MAME gottlieb/gottlieb.cpp. The 8088 main CPU runs
// on cpus/i8086 (FabGL port). Frame:
//
//   run_frame(): NMI (vblank) --> cpuRun() --> handler ... iret --> main
//                                 thread runs to its frame wait --> return
//
// The main thread never runs past its frame wait (MAME traces: every NMI
// hits it), so one cpuRun() per frame matches the hardware. A frame that
// does not reach the wait in BUDGET instructions is cut there instead and
// the next NMI interrupts it, as on the board.

// memory (RAMSIZE) layout: 8088 RAM 0000-2fff at its own address, then the
// video chips' RAM
#define QBERT_RAM_END  0x3000
#define QBERT_VRAM     0x3000   // 3800-3bff tile codes (mirrored at 3c00)
#define QBERT_SPRITES  0x3400   // 3000-30ff sprite list, 64 x 4 (write only)
#define QBERT_PALETTE  0x3500   // 5000-501f, 16 x (gb, r)
#define QBERT_MEM_END  0x3520
static_assert(QBERT_MEM_END <= RAMSIZE, "qbert memory layout exceeds RAMSIZE");

// render input latched at vblank
struct QbertVideoState {
  uint8_t vram[0x400];   // 3800-3bff tile codes
  uint8_t spr[0x100];    // 3000-30ff: y, x, code ^ 0xff, unused
  uint8_t pal[0x20];     // 5000-501f: (g << 4 | b, r) per pen
  uint8_t ctrl;          // 0x5803 latch
};

class qbert : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> qbert_main_rom;
  Asset<uint32_t[8], COMPRESSED> qbert_tiles;
  Asset<uint32_t[32], COMPRESSED> qbert_sprites;
  Asset<unsigned char, COMPRESSED> qbert_levels;
  Asset<unsigned char, COMPRESSED> qbert_sound_rom;
  Asset<unsigned char, COMPRESSED> qbert_sc01;

public:
  qbert();
  signed char machineType() override { return MCH_QBERT; }
  void reset() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  void renderAudio(int16_t *out, int n) override;
  const int renderWidth() override { return 240; }
  const int renderBuffer() override { return 240 * 2 * 8; }
  static const Logo &logo(void) { return qbert_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  enum CpuExit : uint8_t { CPU_WAIT, CPU_BUDGET, CPU_TRAP };

  static const uint16_t ROM_BASE = 0xa000;
  static const uint16_t UNMAPPED = 0x6000;   // 6000-9fff: empty ROM sockets
  // 8088 instructions per frame: 81410 cycles / 4, the shortest instruction
  // with its fetch. MAME traces peak at 4254.
  static const int32_t BUDGET = 20000;

  CpuExit cpuRun(int32_t budget);
  bool frameWait(void) const;
  void enterNmi(void);
  void runCpu(void);
  void publishVideo(void);
  void runSound(void);

  uint8_t rdio(uint16_t a);
  void wrio(uint16_t a, uint8_t v);
  uint8_t readIn1(unsigned int keys) const;
  uint8_t readStick(unsigned int keys) const;

  // 8088 bus; RAM and ROM inline, the rest through rdio/wrio
  uint8_t rd8(uint16_t a) {
    if (a < QBERT_RAM_END) {
      return ram[a];
    }
    if (a >= ROM_BASE) {
      return rom[a - ROM_BASE];
    }
    return a >= UNMAPPED ? 0 : rdio(a);
  }

  void wr8(uint16_t a, uint8_t v) {
    if (a < QBERT_RAM_END) {
      ram[a] = v;
      return;
    }
    wrio(a, v);
  }

  static uint8_t busRead(void *m, uint32_t a) { return ((qbert *)m)->rd8((uint16_t)a); }
  static void busWrite(void *m, uint32_t a, uint8_t v) { ((qbert *)m)->wr8((uint16_t)a, v); }

  uint8_t *ram = nullptr;          // = memory
  const uint8_t *rom = nullptr;    // 8088 0xa000-0xffff
  bool booted = false;             // reset code ran to the first frame wait
  uint32_t trapCount = 0;
  uint32_t budgetCuts = 0;
  int32_t budgetLeft = 0;          // of the last cpuRun()

  const uint32_t (*tiles)[8] = nullptr;
  const uint32_t (*sprites)[32] = nullptr;
  const uint8_t *levels = nullptr;
  Seqlock<QbertVideoState> *vblank = nullptr;   // Arena
  QbertVideoState *video = nullptr;             // Arena, render copy
  uint16_t pens[gottlieb_video::PENS] = {};
  gottlieb_video::Sprite spriteList[gottlieb_video::SPRITES];
  int spriteCount = 0;

  // sound board: commands queued by the main CPU, run after it each frame
  static const uint8_t CMD_QUEUE = 8;
  GottliebSound sound;
  uint8_t cmdQueue[CMD_QUEUE];
  uint8_t cmdCount = 0;
  uint32_t soundPhase = 0;         // frame length remainder, see runSound()
  DcBlock dc;
  int16_t lastSample = 0;
  uint8_t outputLatch = 0;         // 0x5803: b0 bg priority, b1/b2 flip, b5 knocker
};

#endif
