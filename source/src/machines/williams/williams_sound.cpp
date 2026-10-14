#include "williams_sound.h"
#include <string.h>

WilliamsSound *WilliamsSound::active = nullptr;

void WilliamsSound::attach(const uint8_t *r, uint16_t base, uint8_t *ram256) {
  rom = r;
  romBase = base;
  ram = ram256;
}

void WilliamsSound::reset() {
  active = this;
  m6803_ext_read_fn = read;
  m6803_ext_write_fn = write;
  memset(ram, 0, RAM_END);
  pia.reset();
  pia.setInputs(0xff, 0xff);
  budget = 0;
  outPhase = 0;
  dacValue = 0x80;
  wr = rd = 0;
  memset(&cpu, 0, sizeof(cpu));
  m6800_reset(&cpu);
}

// snd_cmd_w: the two high bits are pulled up; CB1 low only for 0xff (idle)
void WilliamsSound::command(uint8_t data) {
  const uint8_t cmd = data | 0xc0;
  pia.setInputs(0xff, cmd);
  pia.cb1(cmd != 0xff);
}

// mainCycles * SOUND_HZ overflows int32 above ~2400 cycles: work in chunks
void WilliamsSound::run(uint32_t mainCycles) {
  static const uint32_t CHUNK = 1024;  // 1024 * 894886 = 916 M < 2^31
  while (mainCycles > 0) {
    const uint32_t n = mainCycles < CHUNK ? mainCycles : CHUNK;
    mainCycles -= n;
    budget += (int32_t)(n * SOUND_HZ);
    while (budget > 0) {
      step();
    }
  }
}

void WilliamsSound::step() {
  // level-held PIA IRQ: m6803_irq() also wakes a WAI
  if (pia.irq()) {
    if (!cpu.irq_pending) {
      m6803_irq(&cpu);
    }
  } else {
    cpu.irq_pending = 0;
  }

  const int cycles = m6800_step(&cpu);
  budget -= cycles * (int32_t)MAIN_HZ;

  // 24 kHz output clock
  outPhase += cycles * OUT_HZ;
  while (outPhase >= SOUND_HZ) {
    outPhase -= SOUND_HZ;
    const uint16_t next = (wr + 1) & (RING - 1);
    if (next != rd) {  // full: drop, the audio core is behind
      ring[wr] = dacValue;
      wr = next;
    }
  }
}

int WilliamsSound::sample() {
  if (rd == wr) {
    return EMPTY;
  }
  const uint8_t v = ring[rd];
  rd = (rd + 1) & (RING - 1);
  return (int)v - 0x80;
}

uint8_t WilliamsSound::read(uint16_t addr) {
  WilliamsSound &s = *active;
  if (addr < RAM_END) {
    return s.ram[addr];
  }
  if ((addr & PIA_MASK) == PIA_BASE) {
    return s.pia.read(addr & 3);
  }
  if (addr >= s.romBase) {
    return s.rom[addr - s.romBase];
  }
  return 0;  // unpopulated ROM sockets
}

void WilliamsSound::write(uint16_t addr, uint8_t val) {
  WilliamsSound &s = *active;
  if (addr < RAM_END) {
    s.ram[addr] = val;
    return;
  }
  if ((addr & PIA_MASK) == PIA_BASE && s.pia.write(addr & 3, val) == Pia6821::PORT_A) {
    s.dacValue = s.pia.outA();
  }
}
