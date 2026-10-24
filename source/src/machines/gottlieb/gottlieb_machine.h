#ifndef GOTTLIEB_MACHINE_H
#define GOTTLIEB_MACHINE_H

#include "../machineBase.h"
#include "../../emulation/dc_block.h"
#include "../../cpus/i8086/i8086.h"
#include "gottlieb_sound.h"
#include "gottlieb_video.h"

// Gottlieb rev. 1 board (MAME gottlieb/gottlieb.cpp), shared by Q*bert, Krull
// and Tylz: 8088 on cpus/i8086, gottlieb_base_map I/O, rev. 1 sound board.
// Frame:
//
//   run_frame(): NMI (vblank) --> cpuRun() --> handler ... iret --> main
//                                 thread runs to its frame wait --> return
//
// A frame that does not reach the wait in BUDGET instructions is cut there
// instead and the next NMI interrupts it, as on the board.
//
// 8088 map: 0000-ramEnd RAM, 3000-5fff video and I/O, ROM codeBase-2fff and
// romBase-ffff (rom[0] = codeBase), the rest of 6000-ffff empty (reads 0).
// memory (RAMSIZE): the 8088 RAM at its own address, then the video chips'
// RAM at ramEnd + *_OFS.

// render input latched at vblank
struct GottliebVideoInput {
  uint8_t vram[0x400];   // 3800-3bff tile codes
  uint8_t spr[0x100];    // 3000-30ff: y, x, code ^ 0xff, unused
  uint8_t pal[0x20];     // 5000-501f: (g << 4 | b, r) per pen
  uint8_t ctrl;          // 0x5803 latch
};

class gottliebMachine : public machineBase {
public:
  // memory[] past ramEnd
  static const uint16_t VRAM_OFS = 0x000;      // 3800-3bff tile codes (mirrored at 3c00)
  static const uint16_t SPRITES_OFS = 0x400;   // 3000-30ff sprite list, 64 x 4 (write only)
  static const uint16_t PALETTE_OFS = 0x500;   // 5000-501f, 16 x (gb, r)
  static const uint16_t CHARRAM_OFS = 0x520;   // 4000-4fff raw bytes, RAM tiles only
  static const uint16_t ROM_TILES_END = 0x520, RAM_TILES_END = 0x1520;

  void reset() override;
  void run_frame(void) override;
  void renderAudio(int16_t *out, int n) override;
  const int renderWidth() override { return gottlieb_video::WIDTH; }
  const int renderBuffer() override { return gottlieb_video::WIDTH * 2 * 8; }

protected:
  // frame wait: IP at waitIp or waitIp2, then either the frame counter word
  // at waitFlag equals AX (waitCounter) or the byte at waitFlag is 0.
  // slowIp: first instruction of a CPU bound loop whose 8088 cycles are far
  // above the budget's average (4 per instruction); each pass costs slowCost
  // budget more, so the loop spans as many frames as on the board. 0: none.
  struct Board {
    uint16_t ramEnd, codeBase, romBase;
    uint16_t waitIp, waitIp2, waitFlag;
    bool waitCounter;
    uint16_t slowIp;
    uint8_t slowCost;
  };

  explicit gottliebMachine(const Board &b) : board(b) {}

  enum CpuExit : uint8_t { CPU_WAIT, CPU_BUDGET, CPU_TRAP };

  // 8088 instructions per frame: 81410 cycles / 4, the shortest instruction
  // with its fetch
  static const int32_t BUDGET = 20000;

  virtual uint8_t readDsw(void) = 0;
  virtual uint8_t readIn1(unsigned int keys) = 0;
  virtual uint8_t readIn4(unsigned int keys) = 0;
  virtual void pollInputs(void) {}
  // vblank: latch the render input of the frame that just ended
  virtual void publishVideo(void) = 0;

  CpuExit cpuRun(int32_t budget);
  bool frameWait(uint16_t ip) const;
  void enterNmi(void);
  void runCpu(void);
  void runSound(void);
  void latchVideo(GottliebVideoInput &v) const;

  uint8_t rdio(uint16_t a);
  void wrio(uint16_t a, uint8_t v);

  // 8088 bus; RAM and ROM inline, the rest through rdio/wrio
  uint8_t rd8(uint16_t a) {
    if (a < board.ramEnd) {
      return ram[a];
    }
    if (a >= board.romBase || (a >= board.codeBase && a < IO_START)) {
      return rom[a - board.codeBase];
    }
    return a >= IO_END ? 0 : rdio(a);
  }

  void wr8(uint16_t a, uint8_t v) {
    if (a < board.ramEnd) {
      ram[a] = v;
      return;
    }
    wrio(a, v);
  }

  static uint8_t busRead(void *m, uint32_t a) { return ((gottliebMachine *)m)->rd8((uint16_t)a); }
  static void busWrite(void *m, uint32_t a, uint8_t v) { ((gottliebMachine *)m)->wr8((uint16_t)a, v); }

  const Board board;
  uint8_t *ram = nullptr;          // = memory
  const uint8_t *rom = nullptr;    // 8088 codeBase-0xffff
  bool booted = false;             // reset code ran to the first frame wait
  uint32_t trapCount = 0;
  uint32_t budgetCuts = 0;
  int32_t budgetLeft = 0;          // of the last cpuRun()

  uint32_t (*charTiles)[8] = nullptr;   // Arena, char RAM as tile words; nullptr: ROM tiles
  const uint32_t (*sprites)[32] = nullptr;
  const uint8_t *levels = nullptr;
  uint16_t pens[gottlieb_video::PENS] = {};
  gottlieb_video::Sprite spriteList[gottlieb_video::SPRITES];
  int spriteCount = 0;

  // sound board: commands queued by the main CPU, run after it each frame
  GottliebSound sound;
  bool soundCmdSeen = false;       // sound command queued this frame, for LEDs
  uint8_t outputLatch = 0;         // 0x5803: b0 bg priority, b1/b2 flip, b5 knocker

private:
  static const uint16_t IO_START = 0x3000, IO_END = 0x6000;
  static const uint8_t CMD_QUEUE = 8;
  uint8_t cmdQueue[CMD_QUEUE];
  uint8_t cmdCount = 0;
  uint32_t soundPhase = 0;         // frame length remainder, see runSound()
  DcBlock dc;
  int16_t lastSample = 0;
};

#endif
