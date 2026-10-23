#ifndef KRULL_H
#define KRULL_H

#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/dc_block.h"
#include "../../emulation/twin_stick.h"
#include "../gottlieb/gottlieb_sound.h"
#include "../../cpus/i8086/i8086.h"
#include "../gottlieb/gottlieb_video.h"
#include "krull_dipswitches.h"
#include "krull_logo.h"

// Krull (Gottlieb 1983), MAME gottlieb/gottlieb.cpp: Q*bert's board with
// program ROM at 1000-2fff and tiles in char RAM. Frame as Q*bert:
//
//   run_frame(): NMI (vblank) --> cpuRun() --> handler ... iret --> main
//                                 thread runs to its idle loop --> return
//
// Game logic runs every 2nd NMI (the handler sets [9E8h]); between, the
// main thread spins in the idle loop at 0x1581.

// memory (RAMSIZE) layout: 8088 RAM 0000-0fff at its own address, then the
// video chips' RAM
#define KRULL_RAM_END  0x1000
#define KRULL_VRAM     0x1000   // 3800-3bff tile codes (mirrored at 3c00)
#define KRULL_SPRITES  0x1400   // 3000-30ff sprite list, 64 x 4 (write only)
#define KRULL_PALETTE  0x1500   // 5000-501f, 16 x (gb, r)
#define KRULL_CHARRAM  0x1520   // 4000-4fff raw bytes, for CPU reads
#define KRULL_MEM_END  0x2520
static_assert(KRULL_MEM_END <= RAMSIZE, "krull memory layout exceeds RAMSIZE");

class krull : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> krull_main_rom;
  Asset<uint32_t[32], COMPRESSED> krull_sprites;
  Asset<unsigned char, COMPRESSED> krull_levels;
  Asset<unsigned char, COMPRESSED> krull_sound_rom;

public:
  krull();
  uint8_t machineType() override { return MCH_KRULL; }
  void reset() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  void renderAudio(int16_t *out, int n) override;
  const int renderWidth() override { return 240; }
  const int renderBuffer() override { return 240 * 2 * 8; }
  static const Logo &logo(void) { return krull_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const int CHAR_TILES = 128;

  // render input latched at vblank, char tiles included: the game rewrites
  // them while they are visible
  struct VideoState {
    uint8_t vram[0x400];                // 3800-3bff tile codes
    uint32_t tiles[CHAR_TILES][8];      // char RAM as tile column words
    uint8_t spr[0x100];                 // 3000-30ff: y, x, code ^ 0xff, unused
    uint8_t pal[0x20];                  // 5000-501f: (g << 4 | b, r) per pen
    uint8_t ctrl;                       // 0x5803 latch
  };

  enum CpuExit : uint8_t { CPU_WAIT, CPU_BUDGET, CPU_TRAP };

  static const uint16_t CODE_BASE = 0x1000;   // 1000-2fff program ROM
  static const uint16_t IO_START = 0x3000;    // 3000-5fff video, I/O
  static const uint16_t ROM_BASE = 0x6000;
  // 8088 instructions per frame: 81410 cycles / 4, the shortest instruction
  // with its fetch
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
  uint8_t readIn4(void) const;

  // 8088 bus; RAM and ROM inline, the rest through rdio/wrio
  uint8_t rd8(uint16_t a) {
    if (a < KRULL_RAM_END) {
      return ram[a];
    }
    if (a < IO_START || a >= ROM_BASE) {
      return rom[a - CODE_BASE];
    }
    return rdio(a);
  }

  void wr8(uint16_t a, uint8_t v) {
    if (a < KRULL_RAM_END) {
      ram[a] = v;
      return;
    }
    wrio(a, v);
  }

  static uint8_t busRead(void *m, uint32_t a) { return ((krull *)m)->rd8((uint16_t)a); }
  static void busWrite(void *m, uint32_t a, uint8_t v) { ((krull *)m)->wr8((uint16_t)a, v); }

  uint8_t *ram = nullptr;          // = memory
  const uint8_t *rom = nullptr;    // 8088 0x1000-0xffff (3000-5fff zero)
  bool booted = false;             // reset code ran to the first frame wait
  uint32_t trapCount = 0;
  uint32_t budgetCuts = 0;
  int32_t budgetLeft = 0;          // of the last cpuRun()

  uint32_t (*charTiles)[8] = nullptr;   // Arena, char RAM as tile words
  const uint32_t (*sprites)[32] = nullptr;
  const uint8_t *levels = nullptr;
  Seqlock<VideoState> *vblank = nullptr;   // Arena
  VideoState *video = nullptr;             // Arena, render copy
  uint16_t pens[gottlieb_video::PENS] = {};
  gottlieb_video::Sprite spriteList[gottlieb_video::SPRITES];
  int spriteCount = 0;

  TwinStick sticks;
  uint16_t stickState = 0;         // sticks.update() of this frame, read by IN4

  // sound board: commands queued by the main CPU, run after it each frame
  static const uint8_t CMD_QUEUE = 8;
  GottliebSound sound;
  uint8_t cmdQueue[CMD_QUEUE];
  uint8_t cmdCount = 0;
  uint32_t soundPhase = 0;         // frame length remainder, see runSound()
  DcBlock dc;
  int16_t lastSample = 0;
  bool soundCmdSeen = false;       // sound command queued this frame, for LEDs
  uint8_t outputLatch = 0;         // 0x5803: b0 bg priority, b1/b2 flip
};

#endif
