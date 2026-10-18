#ifndef WILLIAMS_SOUND_H
#define WILLIAMS_SOUND_H

#include <stdint.h>
#include "../../cpus/m6803/m6803.h"
#include "pia6821.h"
#include "../../emulation/hc55516.h"

// Williams sound board: M6808 @ 894886 Hz, PIA (port A -> MC1408 DAC,
// port B <- command), RAM 0x0000-0x00ff, ROM up to 0xb000-0xffff. Stepped
// on the emulation core after each main CPU slice; DAC values go to a ring
// at 24 kHz that the audio renderer drains (circusc pattern). Speech:
// HC-55516 on the PIA's CA2 (data) / CB2 (clock), Sinistar.
class WilliamsSound {
public:
  static const int RING = 1024;
  static const int EMPTY = 0x7fff;  // sample(): ring empty

  void attach(const uint8_t *rom, uint16_t romBase, uint8_t *ram256, bool speech = false);
  void reset();
  void command(uint8_t data);
  void run(uint32_t mainCycles);
  int sample();
  uint8_t dac() const { return dacValue; }

private:
  static const uint32_t MAIN_HZ = 1000000;
  static const uint32_t SOUND_HZ = 894886;
  static const uint32_t OUT_HZ = 24000;
  static const uint16_t PIA_MASK = 0x7ffc, PIA_BASE = 0x0400;  // mirror 0x8000
  static const uint16_t RAM_END = 0x0100;
  static const uint32_t DELAY_PASS = 6;  // DECA 2 + BNE 4 cycles

  void step();
  int skipDelay();
  int skipIdle(int32_t owed);
  void push(int16_t v);
  int16_t level() const;
  static uint8_t read(uint16_t addr);
  static void write(uint16_t addr, uint8_t val);
  static WilliamsSound *active;  // m6803 callbacks carry no context

  m6803_state cpu;
  Pia6821 pia;
  const uint8_t *rom = nullptr;
  uint16_t romBase = 0;
  uint16_t delayPc = 0;     // DECA of the GWAVE delay loop, 0: none
  m6803_state idle;         // state after the last backward jump
  uint32_t idleCycles = 0;  // cycles since then
  bool touched = false;     // memory write or PIA access since then
  uint8_t *ram = nullptr;
  int32_t budget = 0;       // 6808 cycles owed, in units of 1 / MAIN_HZ
  uint32_t outPhase = 0;    // 24 kHz sample clock, in units of 1 / SOUND_HZ
  int32_t dacArea = 0;      // output level x outPhase units since the last sample
  uint8_t dacValue = 0x80;
  Hc55516 cvsd;
  bool speech = false;
  volatile int16_t ring[RING];
  volatile uint16_t wr = 0, rd = 0;
};

#endif
