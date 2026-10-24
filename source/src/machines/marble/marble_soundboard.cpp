#include "marble_soundboard.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

#include <string.h>
#include "../../emulation/audio_mix.h"

static const uint32_t OUT_RATE = 24000;
// 6502 cycles per 60 Hz frame (MAME: 456 x 262 pixels at 7.16 MHz = 29868 cycles at
// 1.79 MHz), one slice per 64-sample audio chunk
static const int32_t FRAME_CYCLES = 29868, SLICES = 6, SLICE_CYCLES = FRAME_CYCLES / SLICES;
static const uint8_t CHIP_PER_CPU = 2;  // YM2151 3.58 MHz, 6502 1.79 MHz

static const uint16_t RAM_END = 0x1000, ROM_START = 0x8000;
static const uint16_t YM_ADDR = 0x1800, YM_DATA = 0x1801, LATCH = 0x1810, STATUS = 0x1820;
static const uint16_t POKEY_START = 0x1870, POKEY_END = 0x187f;
static const uint8_t POKEY_SKCTL = 0x0f;
// $1820: self test off, coins up; bit 3: a command waits in the latch
static const uint8_t STATUS_IDLE = 0x87, STATUS_COMMAND = 0x08;
static const uint8_t OPEN_BUS = 0xff;

// YM2151 timer registers and control bits
static const uint8_t YM_TIMER_A_HI = 0x10, YM_TIMER_B = 0x12, YM_CONTROL = 0x14;
static const uint8_t RUN_A = 0x01, RUN_B = 0x02, FLAG_A = 0x04, FLAG_B = 0x08;
static const uint8_t CLEAR_A = 0x10, CLEAR_B = 0x20;
static const uint8_t STATUS_A = 0x01, STATUS_B = 0x02;

// output levels against the C64 music, from MAME: race music RMS 1800, effects peak 1900-4200
// (YM2151), 1400-2200 (POKEY). FM sum >> FM_SHIFT, POKEY (+-480) x 3/4
static const uint8_t FM_SHIFT = 6;
static const int32_t POKEY_MUL = 3, POKEY_SHIFT = 2;

void MarbleSoundBoard::reset(const uint8_t *program) {
  rom = program;
  memset(ram, 0, sizeof(ram));
  memset(pokey_regs, 0, sizeof(pokey_regs));
  fm.reset();
  opm.reset();
  pokey.reset(Pokey::CLOCKS_1789KHZ);

  latch = 0;
  pending = false;
  head = count = 0;
  ym_addr = ym_control = ym_status = 0;
  memset(ym_timer, 0, sizeof(ym_timer));
  timer_a = timer_b = 0;
  debt = 0;

  memset(&cpu, 0, sizeof(cpu));
  cpu.read = read;
  cpu.write = write;
  cpu.user = this;
  m6502_reset(&cpu);
}

void MarbleSoundBoard::command(uint8_t c) {
  if (count == QUEUE) { return; }
  queue[(head + count) % QUEUE] = c;
  count++;
}

// LFO first: its pitch writes belong to the frame's first chunk
void MarbleSoundBoard::runFrame() {
  opm.frame();
  for (int32_t s = 0; s < SLICES; s++) {
    slice(SLICE_CYCLES);
    fm.writeMark();
  }
}

void MarbleSoundBoard::render(int16_t *out, int n) {
  for (int at = 0; at < n; at += AUDIO_CHUNK) {
    const int m = n - at < AUDIO_CHUNK ? n - at : AUDIO_CHUNK;
    int32_t fm_out[AUDIO_CHUNK] = {};
    int16_t pokey_out[AUDIO_CHUNK];
    fm.render(fm_out, m, OUT_RATE);
    pokey.render(pokey_regs, pokey_out, m);
    for (int i = 0; i < m; i++) {
      out[at + i] += (int16_t)((fm_out[i] >> FM_SHIFT) + ((pokey_out[i] * POKEY_MUL) >> POKEY_SHIFT));
    }
  }
}

// the next command into the free latch: the NMI handler reads it from $1810
void MarbleSoundBoard::feed() {
  if (pending || !count) { return; }
  latch = queue[head];
  head = (head + 1) % QUEUE;
  count--;
  pending = true;
  cpu.nmi = 1;
}

// cycles of 6502 time; an instruction past the end is paid by the next slice
void MarbleSoundBoard::slice(int32_t cycles) {
  const int32_t target = cycles - debt;
  int32_t done = 0;
  while (done < target) {
    feed();
    const int c = m6502_step(&cpu);
    done += c;
    timers(c * CHIP_PER_CPU);
  }
  debt = done - target;
}

int32_t MarbleSoundBoard::periodA() const {
  return 64 * (1024 - (ym_timer[0] << 2 | (ym_timer[1] & 3)));
}
int32_t MarbleSoundBoard::periodB() const { return 1024 * (256 - ym_timer[2]); }

// counting timers set their status flag at overflow when enabled; a flag holds the IRQ
void MarbleSoundBoard::timers(int32_t chip_clocks) {
  if (timer_a) {
    timer_a -= chip_clocks;
    while (timer_a <= 0) {
      timer_a += periodA();
      if (ym_control & FLAG_A) { ym_status |= STATUS_A; }
    }
  }
  if (timer_b) {
    timer_b -= chip_clocks;
    while (timer_b <= 0) {
      timer_b += periodB();
      if (ym_control & FLAG_B) { ym_status |= STATUS_B; }
    }
  }
  cpu.irq = ym_status ? 1 : 0;
}

// register 0x14: a run bit starts its timer from the full period, clearing it stops the timer
void MarbleSoundBoard::ymControl(uint8_t value) {
  if (!(value & RUN_A)) {
    timer_a = 0;
  } else if (!timer_a) {
    timer_a = periodA();
  }
  if (!(value & RUN_B)) {
    timer_b = 0;
  } else if (!timer_b) {
    timer_b = periodB();
  }
  if (value & CLEAR_A) { ym_status &= ~STATUS_A; }
  if (value & CLEAR_B) { ym_status &= ~STATUS_B; }
  ym_control = value;
  cpu.irq = ym_status ? 1 : 0;
}

void MarbleSoundBoard::ymWrite(uint8_t value) {
  if (ym_addr >= YM_TIMER_A_HI && ym_addr <= YM_TIMER_B) {
    ym_timer[ym_addr - YM_TIMER_A_HI] = value;
    return;
  }
  if (ym_addr == YM_CONTROL) {
    ymControl(value);
    return;
  }
  opm.write(ym_addr, value);
}

uint8_t MarbleSoundBoard::read(m6502_t *cpu, uint16_t addr) {
  MarbleSoundBoard &b = *(MarbleSoundBoard *)cpu->user;
  if (addr >= ROM_START) { return b.rom[addr - ROM_START]; }
  if (addr < RAM_END) { return b.ram[addr]; }

  switch (addr) {
    case YM_ADDR:
    case YM_DATA: return b.ym_status;
    case LATCH: b.pending = false; return b.latch;
    case STATUS: return STATUS_IDLE | (b.pending ? STATUS_COMMAND : 0);
    default: return OPEN_BUS;  // VIA, POKEY reads (unused by the effects)
  }
}

void MarbleSoundBoard::write(m6502_t *cpu, uint16_t addr, uint8_t value) {
  MarbleSoundBoard &b = *(MarbleSoundBoard *)cpu->user;
  if (addr < RAM_END) {
    b.ram[addr] = value;
    return;
  }
  if (addr == YM_ADDR) {
    b.ym_addr = value;
    return;
  }
  if (addr == YM_DATA) {
    b.ymWrite(value);
    return;
  }
  if (addr < POKEY_START || addr > POKEY_END) { return; }  // latches, reply to the 68010

  const uint8_t r = addr - POKEY_START;
  if (r <= Pokey::REG_AUDCTL) { b.pokey_regs[r] = value; }
  if (r == POKEY_SKCTL) { b.pokey_regs[Pokey::REG_SKCTL] = value; }
}

#endif  // GAME_MARBLE
