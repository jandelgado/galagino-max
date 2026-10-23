#include "i8039_dac.h"
#include "audio_mix.h"

static const int32_t SAMPLE_RATE = 24000;

// MAME mcs48.cpp burn_cycles() per opcode
static const uint8_t CYCLES[256] = {
    1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

void I8039Dac::reset(const uint8_t *program) {
  rom = program;
  i8048_reset(&cpu);
  cpu.rom_direct = rom;
  cpu.rom_mask = 0x0fff;
  debt = phase = 0;
  wr = rd = 0;
  dac_val = dac_last = 0x80;
}

// i8039 for this many cycles; P1 sampled each time 1 / 24 kHz of i8039
// time has passed (phase counts cycles * SAMPLE_RATE against hz)
void I8039Dac::run(int32_t cycles) {
  debt += cycles * SAMPLE_RATE;
  while (debt > 0) {
    const int32_t used = CYCLES[rom[cpu.PC & 0x0fff]] * SAMPLE_RATE;
    i8048_step(&cpu);
    debt -= used;
    phase += used;
    while (phase >= hz) {
      phase -= hz;
      const uint16_t next = (wr + 1) & (RING - 1);
      if (next != rd) {   // full: drop
        ring[wr] = dac_val;
        wr = next;
      }
    }
  }
}

void I8039Dac::render(int16_t *out, int n) {
  for (int i = 0; i < n; i++) {
    if (rd != wr) {
      dac_last = ring[rd];
      rd = (rd + 1) & (RING - 1);
    }
    out[i] = audioClamp(out[i] + dc.filter(((int)dac_last - 0x80) * gain), AUDIO_MAX);
  }
}
