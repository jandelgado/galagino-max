#include "williams_sound.h"
#include <string.h>

WilliamsSound *WilliamsSound::active = nullptr;

// speech on the DAC scale: MAME mixes DAC 0.25, CVSD 0.8; tuned on device
static const int32_t SPEECH_GAIN = 192;  // CVSD full scale -> +-192

void WilliamsSound::attach(const uint8_t *r, uint16_t base, uint8_t *ram256, bool sp) {
  rom = r;
  speech = sp;
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
  dacArea = 0;
  dacValue = 0x80;
  cvsd.reset();
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

  const int32_t held = level();  // a DAC or speech change lands at the end of the step
  const int cycles = m6800_step(&cpu);
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
