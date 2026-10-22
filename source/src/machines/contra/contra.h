#ifndef CONTRA_H
#define CONTRA_H

#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"
#include "contra_dipswitches.h"
#include "contra_logo.h"

// Contra (Konami 1987), MAME konami/contra.cpp, set contra.
//
//   HD6309 3 MHz (run as 6809: the 68B09 bootleg runs the same ROM)
//     |  0000-00bf  K007121 #0/#1 registers, 007452 math, inputs, latch
//     |  0c00-5fff  palette, RAM, tile/sprite RAM of both K007121
//     |  6000-7fff  16 x 8 KB ROM banks, 8000-ffff fixed ROM
//     +--latch 001c--> sound 6809 + YM2151: not emulated, Gryzor.sid
//
// The CPU sees ram[] for 0000-5fff (addresses = offsets).

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

public:
  contra();
  signed char machineType() override { return MCH_CONTRA; }
  void reset() override;

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  void run_frame(void) override;
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
  static const Logo &logo(void) { return contra_logo; }

private:
  static const int CHIPS = 2;
  static const int CTRL_REGS = 8;
  static const int MATH_REGS = 6;

  uint8_t readPort(uint16_t addr);
  void writeCtrl(int chip, uint8_t reg, uint8_t val);
  void writeMath(uint8_t reg, uint8_t val);
  uint8_t readMath(uint8_t reg);

  m6809_state main_cpu;
  const uint8_t *main_rom = nullptr;
  uint8_t *ram = nullptr;              // Arena, CONTRA_RAM_END bytes

  uint8_t ctrl[CHIPS][CTRL_REGS] = {};  // K007121 control registers
  uint8_t bank = 0;                     // 6000-7fff ROM bank
  bool irq_line = false;                // K007121 #0 vblank IRQ, level

  // 007452: w0 * w1 -> r0/r1; w4w5 / w2w3 -> r4/r5 quotient, r2/r3 remainder
  uint8_t math_regs[MATH_REGS] = {};
  uint16_t product = 0, quotient = 0, remainder = 0;
};

#endif
