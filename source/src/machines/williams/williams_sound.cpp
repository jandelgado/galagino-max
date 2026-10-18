#include "williams_sound.h"
#include <string.h>

WilliamsSound *WilliamsSound::active = nullptr;

// speech on the DAC scale: MAME mixes DAC 0.25, CVSD 0.8; tuned on device
static const int32_t SPEECH_GAIN = 192;  // CVSD full scale -> +-192

// GWAVE's delay `LDAA dir; DECA; BNE *-1; LDAA 0,X`: the 6808's hottest
// code (Sinistar 67 %, Robotron 52 % of its opcodes)
static const uint8_t GWAVE_DELAY[] = { 0x96, 0x00, 0x4a, 0x26, 0xfd, 0xa6, 0x00 };
static const uint32_t GWAVE_ANY = 1;  // direct page address

void WilliamsSound::attach(const uint8_t *r, uint16_t base, uint8_t *ram256, bool sp) {
  rom = r;
  speech = sp;
  romBase = base;
  ram = ram256;
  delayPc = 0;
  const uint32_t len = 0x10000 - base;
  for (uint32_t a = 0; a + sizeof(GWAVE_DELAY) <= len; a++) {
    uint32_t i = 0;
    while (i < sizeof(GWAVE_DELAY) && (i == GWAVE_ANY || rom[a + i] == GWAVE_DELAY[i])) {
      i++;
    }
    if (i == sizeof(GWAVE_DELAY)) {
      delayPc = (uint16_t)(base + a + 2);
      return;
    }
  }
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
  dacArea = 0;
  dacValue = 0x80;
  cvsd.reset();
  wr = rd = 0;
  memset(&cpu, 0, sizeof(cpu));
  m6800_reset(&cpu);
  idleCycles = 0;
  touched = true;  // no pass recorded yet
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

  const int32_t held = level();  // a DAC or speech change lands at the end of the step
  const uint16_t pc = cpu.PC;
  // an IRQ entry at the loop is stepped: m6800_step() takes it
  int cycles = cpu.PC == delayPc && !(cpu.irq_pending && !(cpu.CC & M6803_CC_I))
                 ? skipDelay() : m6800_step(&cpu);
  idleCycles += cycles;
  if (cpu.PC <= pc) {  // backward jump: a loop pass ended
    cycles += skipIdle(budget - cycles * (int32_t)MAIN_HZ);
  }
  budget -= cycles * (int32_t)MAIN_HZ;

  // 24 kHz output: the DAC level averaged over each sample period. The
  // sound ROM rewrites the DAC up to every 18 cycles (50 kHz); taking one
  // value per period would alias that into the audible band.
  uint32_t phase = cycles * OUT_HZ;
  while (outPhase + phase >= SOUND_HZ) {
    const uint32_t part = SOUND_HZ - outPhase;
    phase -= part;
    outPhase = 0;
    dacArea += held * (int32_t)part;
    push((int16_t)(dacArea / (int32_t)SOUND_HZ));
    dacArea = 0;
  }
  outPhase += phase;
  dacArea += held * (int32_t)phase;
}

// The delay passes that fit the cycles still owed, at once: the loop only
// counts A down, the DAC holds. A pass that does not fit is stepped, so the
// 6808 stops where stepping would and sees PIA changes at the same cycle.
int WilliamsSound::skipDelay() {
  uint32_t passes = cpu.A ? cpu.A : 256;
  const uint32_t fit = (uint32_t)budget / (DELAY_PASS * MAIN_HZ);
  if (passes > fit) {
    passes = fit;
  }
  if (!passes) {
    return m6800_step(&cpu);
  }
  const uint8_t last = (uint8_t)(cpu.A - passes + 1);  // A at the final DECA
  cpu.A = (uint8_t)(cpu.A - passes);
  cpu.CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_V);
  if (last == 0x80) {
    cpu.CC |= M6803_CC_V;
  }
  if (!cpu.A) {
    cpu.CC |= M6803_CC_Z;
  }
  if (cpu.A & 0x80) {
    cpu.CC |= M6803_CC_N;
  }
  cpu.PC = cpu.A ? delayPc : delayPc + 3;
  return (int)(passes * DELAY_PASS);
}

// A pass that came back to the same PC with the same registers, without
// writes or PIA accesses, repeats unchanged until an IRQ (Sinistar's
// TST/BEQ flag poll, Robotron's BRA *). The IRQ line only moves between
// run() calls, so the whole passes that fit the budget run at once.
int WilliamsSound::skipIdle(int32_t owed) {
  const bool same = !touched && cpu.PC == idle.PC && cpu.A == idle.A && cpu.B == idle.B &&
                    cpu.X == idle.X && cpu.SP == idle.SP && cpu.CC == idle.CC &&
                    cpu.wai_state == idle.wai_state;
  const uint32_t pass = idleCycles;
  idle = cpu;
  idleCycles = 0;
  touched = false;
  // an unmasked IRQ line is taken on the next step
  if (!same || owed <= 0 || (pia.irq() && !(cpu.CC & M6803_CC_I))) {
    return 0;
  }
  return (int)((uint32_t)owed / MAIN_HZ / pass * pass);
}

int16_t WilliamsSound::level() const {
  int32_t v = (int32_t)dacValue - 0x80;
  if (speech) {
    v += ((int32_t)cvsd.output() * SPEECH_GAIN) >> 15;
  }
  return (int16_t)v;
}

void WilliamsSound::push(int16_t v) {
  const uint16_t next = (wr + 1) & (RING - 1);
  if (next == rd) {
    return;  // full: drop, the audio core is behind
  }
  ring[wr] = v;
  wr = next;
}

int WilliamsSound::sample() {
  if (rd == wr) {
    return EMPTY;
  }
  const int16_t v = ring[rd];
  rd = (rd + 1) & (RING - 1);
  return v;
}

uint8_t WilliamsSound::read(uint16_t addr) {
  WilliamsSound &s = *active;
  if (addr < RAM_END) {
    return s.ram[addr];
  }
  if ((addr & PIA_MASK) == PIA_BASE) {
    s.touched = true;
    return s.pia.read(addr & 3);
  }
  if (addr >= s.romBase) {
    return s.rom[addr - s.romBase];
  }
  return 0;  // unpopulated ROM sockets
}

void WilliamsSound::write(uint16_t addr, uint8_t val) {
  WilliamsSound &s = *active;
  s.touched = true;
  if (addr < RAM_END) {
    s.ram[addr] = val;
    return;
  }
  if ((addr & PIA_MASK) == PIA_BASE) {
    const Pia6821::Port port = s.pia.write(addr & 3, val);
    if (port == Pia6821::PORT_A) {
      s.dacValue = s.pia.outA();
    } else if (s.speech && port == Pia6821::PORT_NONE && (addr & 1)) {
      s.cvsd.digit(s.pia.ca2());  // CRA/CRB write: the C2 lines may have moved
      s.cvsd.clock(s.pia.cb2());
    }
  }
}
