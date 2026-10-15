#include "bwidowMachine.h"
#include "../../emulation/pokey.h"

// POKEY registers beyond AUDF1..AUDC4 (0-7)
enum : uint8_t { POKEY_AUDCTL = 0x08, POKEY_ALLPOT = 0x08, POKEY_RANDOM = 0x0a, POKEY_SKCTL = 0x0f };
enum : uint8_t { SK_RESET = 0x03, AUDCTL_POLY9 = 0x80 };
// AVG 12.096 MHz, CPU 12.096 MHz / 8
static const uint32_t AVG_CLOCKS_PER_CPU_CLOCK = 8;

bwidowMachine::bwidowMachine(uint8_t (*read)(m6502_t *, uint16_t),
                             void (*write)(m6502_t *, uint16_t, uint8_t),
                             const Map &map, const bwidow_layout::Screen &screen,
                             uint8_t dsw0, uint8_t dsw1)
  : dvgMachine(read, write, IRQ), map(map), screen(screen), dsw{dsw0, dsw1} {
  bwidow_layout::palette(pal);
}

void bwidowMachine::reset() {
  dvgMachine::reset();
  earom.reset();
  avg_state.reset(int32_t(screen.xcenter) << 16, int32_t(screen.ycenter) << 16);
  avg_done_at = 0;
  flip = 0;
  for (uint8_t n = 0; n < POKEYS; n++) {
    allpot[n] = 0;
    random_base[n] = 0;
  }
}

bool bwidowMachine::pokey_running(uint8_t n) const {
  return soundregs[n * Pokey::REG_COUNT + Pokey::REG_SKCTL] & SK_RESET;
}

// ALLPOT: MAME allpot_r callback (DIP switches) while running, else the
// last value
uint8_t bwidowMachine::pokey_read(uint8_t n, uint8_t reg) {
  if (reg == POKEY_ALLPOT) {
    if (pokey_running(n)) {
      allpot[n] = dsw[n];
    }
    return allpot[n];
  }
  if (reg == POKEY_RANDOM) {
    const bool poly9 = soundregs[n * Pokey::REG_COUNT + Pokey::REG_AUDCTL] & AUDCTL_POLY9;
    return Pokey::random(pokey_running(n) ? cpu_cycles() - random_base[n] : 0, poly9);
  }
  return 0;
}

void bwidowMachine::pokey_write(uint8_t n, uint8_t reg, uint8_t v) {
  uint8_t *regs = soundregs + n * Pokey::REG_COUNT;
  if (reg <= POKEY_AUDCTL) {
    regs[reg] = v;
  } else if (reg == POKEY_SKCTL) {
    if (!pokey_running(n) && (v & SK_RESET)) {
      random_base[n] = cpu_cycles();
    }
    regs[Pokey::REG_SKCTL] = v;
  }
}

// Er2055::control() takes the Centipede wiring (C1 = !b1, C2 = b2); this
// board has C1 = !b2, C2 = b1
void bwidowMachine::earom_control(uint8_t v) {
  earom.control((v & ~0x06) | ((v & 0x02) << 1) | ((v & 0x04) >> 1));
}

// MAME avg_vgrst, halts the AVG
void bwidowMachine::avg_reset() {
  avg_state.scale = avg_state.bin_scale = avg_state.color = 0;
  avg_done_at = cpu_cycles();
}

uint16_t bwidowMachine::decode_frame(const uint8_t *vram) {
  const avg::Memory m = {vram, map.vram_size, vrom, uint16_t(map.vrom - map.vram), map.vrom_size,
                         0, 0, avg::COLOR, nullptr};
  const uint16_t count = avg::decode(m, avg_state, work, MAX_LINES);
  avg_done_at = cpu_cycles() + avg_state.draw_clocks / AVG_CLOCKS_PER_CPU_CLOCK;
  return count;
}

uint16_t bwidowMachine::map_lines(uint16_t count) {
  return bwidow_layout::map(screen, work, count, flip);
}
