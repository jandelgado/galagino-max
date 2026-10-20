#ifndef MCR_SSIO_H
#define MCR_SSIO_H

#include <stdint.h>
#include "../../cpus/z80/Z80.h"
#include "../../emulation/ay8910.h"

// Midway MCR Super Sound I/O board (MAME midway.cpp), shared by tron,
// dotron and tapper: Z80 at 2 MHz, four command latches from the main CPU, a status
// byte back, two AY-8910.
// Each AY voice is gated by a duty cycle (AY port writes) that MAME treats
// as volume; AY1 port B bit 7 mutes everything.
//
//   main CPU  --OUT 1c-1f-->  latch[4]  --rd 9000-->  sound Z80
//             <--IN 07-----   status    <--wr c000--
class McrSsio {
public:
  static const int RAM_SIZE = 1024;

  // rom: romSize bytes at 0x0000 (tron 12 KB, dotron 16 KB), ram: RAM_SIZE
  // bytes, dutyGain: duty nibble -> gain/16
  void attach(const uint8_t *rom, uint16_t romSize, uint8_t *ram, const uint8_t *dutyGain);
  void reset();

  void latch(uint8_t n, uint8_t v) { latches[n & 3] = v; }
  uint8_t status() const { return statusByte; }

  // step the sound Z80 for a number of CPU cycles (caller selects the CPU)
  void run(Z80 &cpu, int cycles);
  void render(int16_t *out, int n);

  uint8_t rd(uint16_t a);
  void wr(uint16_t a, uint8_t v);

private:
  static const int CYCLES_PER_COUNT = 40;  // 2 MHz / 50 kHz 14024 clock
  static const uint16_t NO_IDLE_LOOP = 0xffff;
  static const uint16_t NO_IDLE_REF = 0xffff;

  void findIdleLoop();
  bool findIdleCmpLoop();
  bool idle(const Z80 &cpu) const;
  void tickCounter(int cycles);
  void applyVolumes();

  const uint8_t *rom = nullptr;
  uint16_t romSize = 0;
  uint8_t *ram = nullptr;
  const uint8_t *dutyGain = nullptr;
  uint16_t idlePc = NO_IDLE_LOOP;  // program wait loop, see findIdleLoop()
  uint16_t idleFlag = 0;           // RAM offset it polls
  uint16_t idleRef = NO_IDLE_REF;  // RAM offset the flag must reach, if any
  int idlePassCycles = 0;
  uint8_t latches[4] = {0, 0, 0, 0};
  uint8_t statusByte = 0;
  uint8_t ayAddr[2] = {0, 0};
  uint8_t regs[2 * Ay8910::REG_COUNT];
  uint8_t count14024 = 0;
  int countCycles = 0;
  int debt = 0;
  bool irqLine = false;
  // gain 80 per step keeps the duty scaling exact (80 * g / 16 = 5 * g);
  // render shifts the sum right by 4: 2 chips * 3 voices * 15 * 5 = 450
  Ay8910 ay[2] = {Ay8910(AY_INC, AY_GAIN), Ay8910(AY_INC, AY_GAIN)};

  static const uint8_t AY_INC = 10;  // 2 MHz / 8 / 24 kHz
  static const uint8_t AY_GAIN = 80;
  static const int MIX_SHIFT = 4;
};

// execute one Z80 instruction, return its cycles. StepZ80 only charges
// prefixed opcodes, so the main opcode's cycles come from Tables.h here.
int mcrStepZ80(Z80 &z);

#endif
