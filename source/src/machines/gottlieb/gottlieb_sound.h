#ifndef GOTTLIEB_SOUND_H
#define GOTTLIEB_SOUND_H

#include <stdint.h>
#include "../../cpus/m6502/m6502.h"
#include "../../emulation/riot6532.h"
#include "../../emulation/votrax.h"

// Gottlieb rev. 1 sound/speech board (MAME shared/gottlieb_a.cpp, r1a):
// 6502 at 894886 Hz, 6532 RIOT, MC1408 DAC, Votrax SC-01-A with a DAC
// driven clock. Runs on the emulation core after the main CPU; DAC and
// speech go to a 24 kHz ring the audio core drains (WilliamsSound pattern).
//
//   main CPU 0x5802 --> command() --> RIOT PA0-5 (inverted), PA7 edge: IRQ
//   6502 1000 DAC, 2000 phone, 3000 speech clock --> Votrax --> request
//   NMI = rising edge of (!PB7 or Votrax request); PB7 in = request
class GottliebSound {
public:
  static const uint32_t SOUND_HZ = 894886;   // 3579545 / 4
  static const int RING = 1024;
  static const int EMPTY = 0x7fff;           // sample(): ring empty

  // rom: 6502 ROM mapped at romBase up to 0x7fff; sc01a: Votrax ROM (512),
  // nullptr = no Votrax (no 2000/3000 devices, NMI from PB7 only); table:
  // Votrax coefficient tables (VotraxSc01::TABLE_BYTES, 4 byte aligned).
  // idlePc: command wait loop (see skipIdle), 0 = no idle skip; it ends
  // when RAM idleFlag is set, idleZero1/2 must be 0, idlePass cycles/pass.
  // dacGain: DAC out = (dac - 0x80) * dacGain / 5
  struct Config {
    const uint8_t *rom;
    uint16_t romBase;
    const uint8_t *sc01a;
    uint8_t *table;
    uint16_t idlePc;
    uint8_t idleFlag, idleZero1, idleZero2;
    uint32_t idlePass;
    int32_t dacGain;
  };
  void attach(const Config &c);
  void reset();
  void command(uint8_t cmd);
  void run(uint32_t cycles);

  // one main CPU frame: its sound commands, then `cycles` 6502 cycles
  void runFrame(const uint8_t *cmds, int n, uint32_t cycles);
  int sample();

#ifdef GOTTLIEB_SOUND_TRACE
  // test hook: every 6502 write to the DAC, Votrax and clock latches
  void (*trace)(uint32_t cycle, uint16_t addr, uint8_t value) = nullptr;
#endif

private:
  static const uint32_t OUT_HZ = 24000;
  static const uint16_t ADDR_MASK = 0x7fff;   // A15 not decoded

  // Votrax output held between its samples
  struct Hold {
    int16_t value = 0;
    void push(int16_t v) { value = v; }
  };

  void step();
  uint32_t skipIdle();
  bool speechRequest() const;
  void updateNmi();
  void output(uint32_t cycles);
  int16_t level() const;
  void push(int16_t v);
  uint8_t paIn() const { return pa; }
  uint8_t pbIn() const;
  static uint8_t read(m6502_t *cpu, uint16_t addr);
  static void write(m6502_t *cpu, uint16_t addr, uint8_t val);

  m6502_t cpu = {};
  Riot6532 riot;
  VotraxSc01 speech;
  Hold speechOut;
  const uint8_t *rom = nullptr;
  Config cfg = {};
  bool hasSpeech = false;
  uint32_t now = 0;           // 6502 cycles since reset (RIOT time base)
  int32_t budget = 0;         // 6502 cycles owed
  uint8_t pa = 0xff;          // RIOT port A inputs (command lines)
  uint8_t dacValue = 0x80;
  bool nmiLine = false;
  uint32_t outPhase = 0;      // 24 kHz sample clock, in 1 / SOUND_HZ units
  int32_t area = 0;           // output level x outPhase units since the last sample
  volatile int16_t ring[RING];
  volatile uint16_t wr = 0, rd = 0;
};

#endif
