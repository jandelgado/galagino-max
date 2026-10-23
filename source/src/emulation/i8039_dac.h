#ifndef I8039_DAC_H
#define I8039_DAC_H

#include <stdint.h>
#include "../cpus/i8048/i8048.h"
#include "dc_block.h"

// Konami sound board i8039 whose P1 drives an 8-bit DAC (megazone,
// junofrst). run() steps the CPU on the emulation core and samples P1 at
// 24 kHz into a ring; render() pops it on the video core. The machine owns
// the bus callbacks and forwards P1 writes to port1().
class I8039Dac {
public:
  static const uint16_t RING = 1024;

  // hz: i8039 machine cycles per second (clock / 15); gain: DAC scale
  I8039Dac(int32_t hz, int gain) : hz(hz), gain(gain) {}

  void reset(const uint8_t *rom);   // 4 KB program, fetched directly
  void run(int32_t cycles);
  void render(int16_t *out, int n);   // adds the DAC to out, clamped
  void port1(uint8_t v) { dac_val = v; }
  void irq(bool assert) { cpu.notINT = !assert; }   // INT pin, active low
  uint16_t queued(void) const { return (wr - rd) & (RING - 1); }

private:
  struct i8048_state_S cpu;
  const int32_t hz;
  const int gain;
  const uint8_t *rom = nullptr;
  int32_t debt = 0;    // cycles * SAMPLE_RATE still to run
  int32_t phase = 0;   // cycles * SAMPLE_RATE since the last sample
  volatile uint8_t ring[RING];
  volatile uint16_t wr = 0, rd = 0;
  uint8_t dac_val = 0x80, dac_last = 0x80;
  DcTrack dc;
};

#endif
