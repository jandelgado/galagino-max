#ifndef QBERT_H
#define QBERT_H

#include "../machineBase.h"
#include "../../cpus/i8086/i8086.h"
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

class qbert : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> qbert_main_rom;

public:
  qbert();
  signed char machineType() override { return MCH_QBERT; }
  void reset() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
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

  uint8_t soundCmd = 0;            // 0x5802, read by the sound board (later)
  uint8_t outputLatch = 0;         // 0x5803: b0 bg priority, b1/b2 flip, b5 knocker
};

#endif
