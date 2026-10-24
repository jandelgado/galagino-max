#ifndef MARBLE_SOUNDBOARD_H
#define MARBLE_SOUNDBOARD_H

#include <stdint.h>
#include "../../cpus/m6502/m6502.h"
#include "../../emulation/pokey.h"
#include "../../emulation/ym2203fm.h"
#include "marble_opm.h"

// Atari System 1 sound board (MARBLE.md "Sound"): the arcade's sound program (136033.421/422)
// on a 6502, for the effects. Its YM2151 plays through MarbleOpm on a YM2203, its POKEY on
// Pokey. The music stays with the C64 tune: the board never gets a music command.
//
//   emulation core                              audio core
//   command(c): queued, latched + NMI           render(out, n): FM up to the next mark,
//   runFrame(): LFO, then 6 slices of 6502        POKEY from the live registers
//     $1800/1 YM2151 -> MarbleOpm -> fm ring
//     slice end -> fm.writeMark()  (one per 64-sample audio chunk)
//
// Memory map (MAME atarisy1, Marble Love sound-mmu.ts):
//   $0000-$0fff RAM, $1800/1 YM2151, $1810 command latch (read), $1820 status,
//   $1870-$187f POKEY, $8000-$ffff ROM. IRQ: YM2151 timers; NMI: a command.
class MarbleSoundBoard {
public:
  MarbleSoundBoard() : fm(FM_CLOCK, true), opm(fm), pokey(Pokey::CLOCKS_1789KHZ) {}

  // both cores stopped: power on with the sound program rom ($8000-$ffff)
  void reset(const uint8_t *rom);
  // emulation core: a sound command from the main CPU
  void command(uint8_t c);
  // emulation core: one 60 Hz frame
  void runFrame();
  // audio core: adds n samples at 24 kHz
  void render(int16_t *out, int n);

private:
  // the OPM's 3.58 MHz / 64 = 55.9 kHz is the YM2203's clock / 72
  static const uint32_t FM_CLOCK = 4026988;
  static const uint16_t RAM_SIZE = 0x1000;
  static const uint8_t QUEUE = 16;

  m6502_t cpu = {};
  const uint8_t *rom = nullptr;
  uint8_t ram[RAM_SIZE];
  Ym2203Fm fm;
  MarbleOpm opm;
  Pokey pokey;
  uint8_t pokey_regs[Pokey::REG_COUNT];

  // command latch (68010 -> 6502) and the commands waiting for it
  uint8_t latch = 0;
  bool pending = false;
  uint8_t queue[QUEUE];
  uint8_t head = 0, count = 0;

  // YM2151 address and timers (ymfm opm: A 64 x (1024 - NA), B 1024 x (256 - NB) chip clocks)
  uint8_t ym_addr = 0, ym_timer[3] = {}, ym_control = 0, ym_status = 0;
  int32_t timer_a = 0, timer_b = 0;  // chip clocks to the overflow, 0: stopped
  int32_t debt = 0;                  // cycles the last slice ran over

  static uint8_t read(m6502_t *cpu, uint16_t addr);
  static void write(m6502_t *cpu, uint16_t addr, uint8_t value);
  void ymWrite(uint8_t value);
  void ymControl(uint8_t value);
  void timers(int32_t chip_clocks);
  int32_t periodA() const;
  int32_t periodB() const;
  void feed();
  void slice(int32_t cycles);
};

#endif
