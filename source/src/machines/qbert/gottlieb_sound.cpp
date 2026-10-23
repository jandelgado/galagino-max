#include "../../catalog.h"
#ifdef GAME_QBERT

#include "gottlieb_sound.h"
#include <string.h>

// 6502 map, r1_map with A15 not decoded (global mask 0x7fff)
static const uint16_t RIOT_RAM_SEL = 0x0200;   // 0000-0fff: A9 0 RAM, 1 I/O
static const uint16_t RIOT_END = 0x1000;
static const uint16_t DAC_BASE = 0x1000, VOTRAX_BASE = 0x2000, CLOCK_BASE = 0x3000;
static const uint16_t DEVICE_END = 0x4000;
static const uint16_t ROM_BASE = 0x6000, ROM_FITTED = 0x7000;   // 6000-6fff: socket empty

// SB1 dip switches on PB0-6 (all open: sound test off); PB7 = Votrax request
static const uint8_t SB1 = 0x7f;
static const uint8_t PB7 = 0x80, PA7 = 0x80, CMD_BITS = 0x3f, CMD_PA6 = 0x40;

// Command wait loop of the sound ROM, 97 % of all its instructions:
//   7035 CLI / LDA #0 / STA $20 / STA $23 / LDA $8202 / AND #$40 / BNE 7046
//   7046 SEI / LDA $21 / BNE 704d / BEQ 7035
// Left only when the IRQ handler sets $21; a pass changes nothing once A,
// $20 and $23 are 0.
static const uint16_t IDLE_PC = 0x7035;
static const uint8_t IDLE_FLAG = 0x21, IDLE_ZERO1 = 0x20, IDLE_ZERO2 = 0x23;
static const uint32_t IDLE_PASS = 29;       // cycles per pass
static const uint32_t ADVANCE_CHUNK = 2048; // Votrax advance(): ticks * clock < 2^32

// speech clock DAC -> Votrax main clock (convert_speech_clock, MAME guesswork)
static const uint32_t SPEECH_CLOCK_BASE = 950000, SPEECH_CLOCK_STEP = 5500;
static const uint8_t SPEECH_CLOCK_MIN = 0x40, SPEECH_CLOCK_MID = 0xa0;

// mix (MAME routes DAC 0.20, Votrax 0.80) onto +-AUDIO_MAX (511)
static const int32_t DAC_GAIN = 4;        // (dac - 0x80) * 4 / 5 = +-102
static const int32_t SPEECH_FULL = 409;   // Votrax FULL_SCALE -> +-409

static uint32_t speechClock(uint8_t v) {
  if (v < SPEECH_CLOCK_MIN) {
    v = SPEECH_CLOCK_MIN;
  }
  return SPEECH_CLOCK_BASE + ((int32_t)v - SPEECH_CLOCK_MID) * (int32_t)SPEECH_CLOCK_STEP;
}

void GottliebSound::attach(const uint8_t *rom4k, const uint8_t *sc01a, uint8_t *table) {
  rom = rom4k;
  speech.init(sc01a, table);
  cpu.read = read;
  cpu.write = write;
  cpu.user = this;
}

void GottliebSound::reset() {
  riot.reset();
  speech.reset();
  speech.setClock(speechClock(0));
  speechOut = Hold();
  now = 0;
  budget = 0;
  pa = 0xff;
  dacValue = 0x80;
  outPhase = 0;
  area = 0;
  wr = rd = 0;
  m6502_reset(&cpu);
  nmiLine = true;   // the Votrax request is high from power on: no NMI edge at reset (as MAME)
  updateNmi();
}

// write_sync: command to PA0-5 inverted, PA7 = low nibble not all ones
// (PA6 is not driven and keeps its pull-up)
void GottliebSound::command(uint8_t cmd) {
  pa = (uint8_t)((~cmd & CMD_BITS) | CMD_PA6 | ((cmd & 0x0f) != 0x0f ? PA7 : 0));
  riot.edge_detect(pa);
}

void GottliebSound::run(uint32_t cycles) {
  budget += (int32_t)cycles;
  while (budget > 0) {
    step();
  }
}

// The main CPU sends its commands within microseconds, in one frame
// burst. Spread over the frame: back to back, a pair like "ff f4 ff f5"
// leaves one PA7 edge and the 6502 reads only the last command.
void GottliebSound::runFrame(const uint8_t *cmds, int n, uint32_t cycles) {
  uint32_t done = 0;
  for (int i = 0; i < n; i++) {
    const uint32_t at = (uint32_t)((uint64_t)cycles * i / n);
    run(at - done);
    done = at;
    command(cmds[i]);
  }
  run(cycles - done);
}

void GottliebSound::step() {
  cpu.irq = riot.irq(now);
  const uint32_t skipped = skipIdle();
  if (skipped) {
    now += skipped;
    budget -= skipped;
    for (uint32_t left = skipped; left;) {
      const uint32_t n = left < ADVANCE_CHUNK ? left : ADVANCE_CHUNK;
      speech.advance(n, SOUND_HZ, speechOut);
      left -= n;
    }
    output(skipped);
    return;
  }

  const int c = m6502_step(&cpu);
  now += c;
  budget -= c;
  speech.advance(c, SOUND_HZ, speechOut);
  updateNmi();
  output(c);
}

// Whole wait loop passes that fit the cycles owed and end before the RIOT
// timer IRQ, at once. Only with nothing pending: no IRQ or NMI, and the
// Votrax request high (no phone in flight, so no NMI edge to come). 0: step.
uint32_t GottliebSound::skipIdle() {
  const bool settled = cpu.a == 0 && (cpu.p & M6502_FI) && riot.ram_read(IDLE_ZERO1) == 0 &&
                       riot.ram_read(IDLE_ZERO2) == 0;
  if (cpu.pc != IDLE_PC || !settled || riot.ram_read(IDLE_FLAG) || cpu.irq || cpu.nmi ||
      !speech.request() || budget <= 0) {
    return 0;
  }
  uint32_t limit = (uint32_t)budget;
  const uint32_t timer = riot.until_timer_irq(now);
  if (timer < limit) {
    limit = timer;
  }
  return (limit / IDLE_PASS) * IDLE_PASS;
}

// input merger "any high": RIOT PB7 inverted, Votrax request; edge triggered
void GottliebSound::updateNmi() {
  const bool line = !(riot.pb_output() & PB7) || speech.request();
  if (line && !nmiLine) {
    cpu.nmi = 1;
  }
  nmiLine = line;
}

int16_t GottliebSound::level() const {
  return (int16_t)((((int32_t)dacValue - 0x80) * DAC_GAIN) / 5 +
                   (speechOut.value * SPEECH_FULL) / VotraxSc01::FULL_SCALE);
}

// 24 kHz output: the level averaged over each sample period (the DAC is
// rewritten at up to ~50 kHz; one value per period would alias)
void GottliebSound::output(uint32_t cycles) {
  const int32_t held = level();
  uint32_t phase = cycles * OUT_HZ;
  while (outPhase + phase >= SOUND_HZ) {
    const uint32_t part = SOUND_HZ - outPhase;
    phase -= part;
    outPhase = 0;
    area += held * (int32_t)part;
    push((int16_t)(area / (int32_t)SOUND_HZ));
    area = 0;
  }
  outPhase += phase;
  area += held * (int32_t)phase;
}

void GottliebSound::push(int16_t v) {
  const uint16_t next = (wr + 1) & (RING - 1);
  if (next == rd) {
    return;   // full: drop, the audio core is behind
  }
  ring[wr] = v;
  wr = next;
}

int GottliebSound::sample() {
  if (rd == wr) {
    return EMPTY;
  }
  const int16_t v = ring[rd];
  rd = (rd + 1) & (RING - 1);
  return v;
}

uint8_t GottliebSound::pbIn() const {
  return SB1 | (speech.request() ? PB7 : 0);
}

uint8_t GottliebSound::read(m6502_t *c, uint16_t addr) {
  GottliebSound &s = *(GottliebSound *)c->user;
  addr &= ADDR_MASK;
  if (addr < RIOT_END) {
    if (!(addr & RIOT_RAM_SEL)) {
      return s.riot.ram_read(addr);
    }
    return s.riot.io_read(addr & 0x1f, s.paIn(), s.pbIn(), s.now);
  }
  if (addr >= ROM_FITTED) {
    return s.rom[addr - ROM_FITTED];
  }
  return addr >= ROM_BASE ? 0 : 0xff;   // empty ROM socket reads 0, open bus 0xff
}

void GottliebSound::write(m6502_t *c, uint16_t addr, uint8_t val) {
  GottliebSound &s = *(GottliebSound *)c->user;
  addr &= ADDR_MASK;
  if (addr < RIOT_END) {
    if (!(addr & RIOT_RAM_SEL)) {
      s.riot.ram_write(addr, val);
      return;
    }
    s.riot.io_write(addr & 0x1f, val, s.paIn(), s.now);
    s.updateNmi();   // PB7 may have moved
    return;
  }
  if (addr >= DEVICE_END) {
    return;
  }
#ifdef GOTTLIEB_SOUND_TRACE
  if (s.trace) {
    s.trace(s.now, addr & 0xf000, val);
  }
#endif
  switch (addr & 0xf000) {
    case DAC_BASE:
      s.dacValue = val;
      break;
    case VOTRAX_BASE:   // votrax_data_w
      s.speech.writePhone(~val & 0x3f, val >> 6);
      s.updateNmi();
      break;
    case CLOCK_BASE:    // speech_clock_dac_w
      s.speech.setClock(speechClock(val));
      break;
  }
}

#endif // GAME_QBERT
