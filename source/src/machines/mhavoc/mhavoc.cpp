#include "mhavoc.h"
#include "mhavoc_rom.h"
#include "mhavoc_avgrom.h"
#include "mhavoc_gamma.h"
#include "mhavoc_vrom.h"

// alpha memory map (MAME mhavoc_state::alpha_map)
enum : uint16_t {
  RAM_LOW_END = 0x0200,
  BANK = 0x0200,      // 0x0200-0x07ff, again at 0x0a00-0x0fff
  RAM_HIGH = 0x0800,  // 0x0800-0x09ff
  BANK2 = 0x0a00,
  RAM_END = 0x1000,
  GAMMA_READ = 0x1000,
  IN0 = 0x1200,
  COLORRAM = 0x1400,  // 0x1400-0x141f
  OUT0 = 0x1600,
  AVG_GO = 0x1640,
  AVG_RESET = 0x16c0,
  ALPHA_IRQ_ACK = 0x1700,
  ROM_PAGE = 0x1740,
  RAM_PAGE = 0x1780,
  GAMMA_WRITE = 0x17c0,
  BETA = 0x1800,      // 0x1800-0x1fff
  PAGED_ROM = 0x2000, // 0x2000-0x3fff
  MH_VRAM = 0x4000,   // 0x4000-0x4fff
  MH_VRAM_SIZE = 0x1000,
  VROM = 0x5000,      // 0x5000-0x5fff, 0x6000-0x6fff mirrored at 0x7000
  VROM_PAGE = 0x6000,
  ROM = 0x8000,
};
// 0x1680 watchdog: ignored

// gamma memory map (MAME mhavoc_state::gamma_map), decoded on A15-A11
enum : uint16_t {
  GAMMA_RAM_MASK = 0x07ff,
  POKEYS_BASE = 0x2000, // 0x2000-0x27ff
  IN1 = 0x2800,
  ALPHA_READ = 0x3000,
  ROLLER = 0x3800,
  DSW2_IRQ_ACK = 0x4000,
  COIN_COUNTERS = 0x4800,
  ALPHA_WRITE = 0x5000,
  EEPROM = 0x6000, // 0x6000-0x7fff, 512 bytes mirrored
  GAMMA_ROM = 0x8000,
  GAMMA_ROM_MASK = 0x3fff,
};

// IN0 (alpha 0x1200)
enum : uint8_t {
  IN0_AVG_DONE = 0x01,
  IN0_CLOCK_2K4 = 0x02,
  IN0_GAMMA_XMTD = 0x04,
  IN0_GAMMA_RCVD = 0x08,
  IN0_IDLE = 0x30, // diag step, service switch: off
  IN0_COIN_SHIFT = 6,
  COIN_IDLE = 0x03, // left (bit 0), right coin: active low
  COIN_LEFT = 0x01,
};
// IN1 (gamma 0x2800)
enum : uint8_t {
  IN1_ALPHA_XMTD = 0x01,
  IN1_ALPHA_RCVD = 0x02,
  IN1_IDLE = 0x3c, // TIRDY, unused, player 2 buttons
  IN1_SHIELD = 0x40,
  IN1_FIRE = 0x80,
};
// out0 (alpha 0x1600)
enum : uint8_t { OUT0_ROLLER_LIGHT = 0x01, OUT0_GAMMA_RUN = 0x08, OUT0_PLAYER_1 = 0x20 };

// POKEY registers beyond AUDF1..AUDC4 (0-7)
enum : uint8_t { MH_POKEY_AUDCTL = 0x08, MH_POKEY_ALLPOT = 0x08, MH_POKEY_RANDOM = 0x0a, MH_POKEY_SKCTL = 0x0f };
enum : uint8_t { MH_SK_RESET = 0x03, MH_AUDCTL_POLY9 = 0x80 };

// MAME 5k_timer (10 MHz / 2048): every 512 alpha (256 gamma) cycles
static const uint32_t TICK = 512;
static const uint8_t ALPHA_IRQ_COUNT = 0x0c, GAMMA_IRQ_BIT = 0x08;
static const uint32_t ALPHA_CYCLES_PER_FRAME = 41667; // 2.5 MHz / 60 Hz
static const uint32_t SLICE = 128;              // alpha cycles between gamma catch-ups
static const uint32_t CLOCK_2K4_BIT = 0x400;    // MAME clock_r: total cycles bit 10
// AVG 12.096 MHz in alpha cycles (2.5 MHz)
static const uint32_t AVG_CLOCKS_NUM = 25, AVG_CLOCKS_DEN = 121;

// START: coin, then FIRE once the game shows PRESS START
static const uint8_t COIN_FRAMES = 6, FIRE_FROM = 40, FIRE_TO = 46;

mhavoc::mhavoc()
  : dvgMachine(alpha_read, alpha_write),
    mhavoc_rom(mhavoc_rom_blob),
    mhavoc_avgrom(mhavoc_avgrom_blob),
    mhavoc_gamma(mhavoc_gamma_blob),
    mhavoc_vrom(mhavoc_vrom_blob) {
  static_assert(RAM_HIGH_OFS + 0x0200 <= VRAM_OFS, "Major Havoc low RAM overlaps VRAM");
  static_assert(MH_VRAM_SIZE <= MAX_VRAM_SIZE, "Major Havoc VRAM exceeds MAX_VRAM_SIZE");
  static_assert(EEPROM_OFS + EEPROM_SIZE <= RAMSIZE, "Major Havoc RAM does not fit");
  // unpack largest first: Arena first fit puts alpha, gamma and vector ROM
  // in block A, the AVG ROM next to the line buffers in block B
  rom = mhavoc_rom.data();
  avgrom = mhavoc_avgrom.data();
  gamma_rom = mhavoc_gamma.data();
  vrom = mhavoc_vrom.data();

  memset(&gamma, 0, sizeof(gamma));
  gamma.read = gamma_read;
  gamma.write = gamma_write;
  gamma.user = this;
  mhavoc_layout::palette(pal);
}

void mhavoc::reset() {
  dvgMachine::reset();
  memset(memory + EEPROM_OFS, 0xff, EEPROM_SIZE); // blank 2804, not persisted
  m6502_reset(&gamma);
  gamma_cycles = 0;
  gamma_debt = 0;
  gamma_held = false;
  alpha_data = gamma_data = 0;
  alpha_rcvd = alpha_xmtd = gamma_rcvd = gamma_xmtd = false;
  player_1 = false;
  out0_value = 0;
  rom_page = ram_page = 0;
  next_tick = TICK;
  alpha_irq_clock = gamma_irq_clock = 0;
  alpha_irq_enable = true;
  avg_state.reset(int32_t(mhavoc_layout::SCREEN.xcenter) << 16,
                  int32_t(mhavoc_layout::SCREEN.ycenter) << 16);
  avg_done_at = 0;
  allpot = 0;
  for (uint8_t n = 0; n < POKEYS; n++) {
    random_base[n] = 0;
  }
  start_timer = 0;
  start_held = false;
}

// MAME cpu_irq_clock: alpha IRQ 12 ticks after its ack, gamma IRQ line =
// counter bit 3
void mhavoc::tick() {
  if (alpha_irq_enable && (++alpha_irq_clock & ALPHA_IRQ_COUNT) == ALPHA_IRQ_COUNT) {
    m_cpu.irq = 1;
    alpha_irq_enable = false;
  }
  gamma.irq = (++gamma_irq_clock & GAMMA_IRQ_BIT) ? 1 : 0;
}

// gamma runs at half the alpha clock: gamma_debt counts alpha cycles it
// is behind
void mhavoc::step_gamma(uint32_t alpha_cycles) {
  gamma_debt += alpha_cycles;
  if (gamma_held) {
    gamma_cycles += gamma_debt / 2;
    gamma_debt &= 1;
    return;
  }
  while (gamma_debt > 1) {
    const int c = m6502_step(&gamma);
    gamma_cycles += c;
    gamma_debt -= 2 * c;
  }
}

void mhavoc::control_input() {
  keys = input->buttons_get();
  roller.step((keys & BUTTON_RIGHT) ? 1 : (keys & BUTTON_LEFT) ? -1 : 0);
  const bool start = keys & BUTTON_START;
  if (start && !start_held) {
    start_timer = 1;
  } else if (start_timer && ++start_timer > FIRE_TO) {
    start_timer = 0;
  }
  start_held = start;
}

void mhavoc::run_frame() {
  control_input();
  const uint32_t end = cycles + ALPHA_CYCLES_PER_FRAME;
  while ((int32_t)(cycles - end) < 0) {
    uint32_t slice_end = cycles + SLICE;
    if ((int32_t)(slice_end - end) > 0) {
      slice_end = end;
    }
    const uint32_t from = cycles;
    while ((int32_t)(cycles - slice_end) < 0) {
      cycles += m6502_step(&m_cpu);
    }
    step_gamma(cycles - from);
    while ((int32_t)(cycles - next_tick) >= 0) {
      tick();
      next_tick += TICK;
    }
  }
}

uint8_t *mhavoc::alpha_ram(uint16_t a) {
  if (a < RAM_LOW_END) {
    return memory + a;
  }
  if (a >= RAM_HIGH && a < BANK2) {
    return memory + RAM_HIGH_OFS + (a - RAM_HIGH);
  }
  const uint16_t ofs = a < RAM_HIGH ? a - BANK : a - BANK2;
  return memory + BANK_OFS + ram_page * BANK_SIZE + ofs;
}

uint8_t mhavoc::in0() const {
  uint8_t coin = COIN_IDLE;
  if ((keys & BUTTON_COIN) || (start_timer && start_timer <= COIN_FRAMES)) {
    coin &= ~COIN_LEFT;
  }
  const uint8_t high = player_1 ? MHAVOC_SERVICE : coin;
  return IN0_IDLE | (high << IN0_COIN_SHIFT) |
         ((int32_t)(cycles - avg_done_at) >= 0 ? IN0_AVG_DONE : 0) |
         ((cycles & CLOCK_2K4_BIT) ? 0 : IN0_CLOCK_2K4) |
         (gamma_xmtd ? IN0_GAMMA_XMTD : 0) | (gamma_rcvd ? IN0_GAMMA_RCVD : 0);
}

uint8_t mhavoc::in1() const {
  uint8_t v = IN1_IDLE | IN1_SHIELD | IN1_FIRE;
  if ((keys & (BUTTON_FIRE | BUTTON_A)) || (start_timer >= FIRE_FROM && start_timer <= FIRE_TO)) {
    v &= ~IN1_FIRE;
  }
  if (keys & (BUTTON_DOWN | BUTTON_B)) {
    v &= ~IN1_SHIELD;
  }
  return v | (alpha_xmtd ? IN1_ALPHA_XMTD : 0) | (alpha_rcvd ? IN1_ALPHA_RCVD : 0);
}

// MAME out_0_w: gamma held in reset while bit 3 is low
void mhavoc::out0(uint8_t v) {
  out0_value = v;
  player_1 = v & OUT0_PLAYER_1;
  const bool held = !(v & OUT0_GAMMA_RUN);
  if (held) {
    alpha_rcvd = alpha_xmtd = gamma_rcvd = gamma_xmtd = false;
  } else if (gamma_held) {
    m6502_reset(&gamma);
  }
  gamma_held = held;
}

uint8_t mhavoc::alpha_read(m6502_t *cpu, uint16_t a) {
  mhavoc *m = static_cast<mhavoc *>(cpu->user);
  if (a >= ROM) {
    return m->rom[a - ROM];
  }
  if (a >= MH_VRAM) {
    if (a < MH_VRAM + MH_VRAM_SIZE) {
      return m->memory[VRAM_OFS + a - MH_VRAM];
    }
    return a < VROM_PAGE ? m->vrom[a - VROM] : m->vrom[0x1000 + (a & 0x0fff)];
  }
  if (a >= PAGED_ROM) {
    return m->rom[0x8000 + m->rom_page * 0x2000 + (a - PAGED_ROM)];
  }
  if (a < RAM_END) {
    return *m->alpha_ram(a);
  }
  if (a >= BETA) {
    return m->memory[BETA_OFS + a - BETA];
  }
  if ((a & ~0x1f) == COLORRAM) {
    return m->memory[COLOR_OFS + (a & 0x1f)];
  }
  switch (a) {
    case GAMMA_READ:
      m->alpha_rcvd = true;
      m->gamma_xmtd = false;
      return m->gamma_data;
    case IN0: return m->in0();
  }
  return 0;
}

void mhavoc::alpha_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  mhavoc *m = static_cast<mhavoc *>(cpu->user);
  if (a < RAM_END) {
    *m->alpha_ram(a) = v;
    return;
  }
  if (a >= MH_VRAM && a < MH_VRAM + MH_VRAM_SIZE) {
    m->memory[VRAM_OFS + a - MH_VRAM] = v;
    return;
  }
  if (a >= BETA && a < PAGED_ROM) {
    m->memory[BETA_OFS + a - BETA] = v;
    return;
  }
  if ((a & ~0x1f) == COLORRAM) {
    m->memory[COLOR_OFS + (a & 0x1f)] = v;
    return;
  }
  switch (a) {
    case OUT0: m->out0(v); break;
    case AVG_GO: m->go(); break;
    case AVG_RESET:
      // MAME mhavoc_vgrst: halts the AVG
      m->avg_state.scale = m->avg_state.bin_scale = m->avg_state.color = 0;
      m->avg_state.sparkle = false;
      m->avg_done_at = m->cycles;
      break;
    case ALPHA_IRQ_ACK:
      m->m_cpu.irq = 0;
      m->alpha_irq_clock = 0;
      m->alpha_irq_enable = true;
      break;
    case ROM_PAGE: m->rom_page = v & 3; break;
    case RAM_PAGE: m->ram_page = v & 1; break;
    case GAMMA_WRITE:
      // MAME delayed_gamma_w: latch and NMI
      m->gamma_rcvd = false;
      m->alpha_xmtd = true;
      m->alpha_data = v;
      m->gamma.nmi = 1;
      break;
  }
}

bool mhavoc::pokey_running(uint8_t n) const {
  return soundregs[n * Pokey::REG_COUNT + Pokey::REG_SKCTL] & MH_SK_RESET;
}

// ALLPOT: POKEY 0 reads DSW1 (MAME allpot_r) while running, else the
// last value; the other POKEYs have no pots
uint8_t mhavoc::pokey_read(uint8_t n, uint8_t reg) {
  if (reg == MH_POKEY_ALLPOT) {
    if (n) {
      return 0;
    }
    if (pokey_running(0)) {
      allpot = MHAVOC_DSW1 | (input->demoSoundsOff() ? MHAVOC_DSW1_DEMO_SOUNDS_OFF : 0);
    }
    return allpot;
  }
  if (reg == MH_POKEY_RANDOM) {
    const bool poly9 = soundregs[n * Pokey::REG_COUNT + Pokey::REG_AUDCTL] & MH_AUDCTL_POLY9;
    return Pokey::random(pokey_running(n) ? gamma_cycles - random_base[n] : 0, poly9);
  }
  return 0;
}

void mhavoc::pokey_write(uint8_t n, uint8_t reg, uint8_t v) {
  uint8_t *regs = soundregs + n * Pokey::REG_COUNT;
  if (reg <= MH_POKEY_AUDCTL) {
    regs[reg] = v;
  } else if (reg == MH_POKEY_SKCTL) {
    if (!pokey_running(n) && (v & MH_SK_RESET)) {
      random_base[n] = gamma_cycles;
    }
    regs[Pokey::REG_SKCTL] = v;
  }
}

// MAME quad_pokeyn_r/w: A0-A2 register, A3-A4 chip, A5 register bit 3
static uint8_t pokey_chip(uint16_t a) { return (a >> 3) & 3; }
static uint8_t pokey_reg(uint16_t a) { return (a & 7) | ((a & 0x20) >> 2); }

uint8_t mhavoc::gamma_read(m6502_t *cpu, uint16_t a) {
  mhavoc *m = static_cast<mhavoc *>(cpu->user);
  if (a >= GAMMA_ROM) {
    return m->gamma_rom[a & GAMMA_ROM_MASK];
  }
  switch (a & 0xf800) {
    case 0x0000:
    case 0x0800:
    case 0x1000:
    case 0x1800: return m->memory[GAMMA_RAM_OFS + (a & GAMMA_RAM_MASK)];
    case POKEYS_BASE: return m->pokey_read(pokey_chip(a), pokey_reg(a));
    case IN1: return m->in1();
    case ALPHA_READ:
      m->gamma_rcvd = true;
      m->alpha_xmtd = false;
      return m->alpha_data;
    case ROLLER: return ~m->roller.count(); // MAME PORT_REVERSE
    case DSW2_IRQ_ACK: return MHAVOC_DSW2;
  }
  if ((a & 0xe000) == EEPROM) {
    return m->memory[EEPROM_OFS + (a & (EEPROM_SIZE - 1))];
  }
  return 0;
}

void mhavoc::gamma_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  mhavoc *m = static_cast<mhavoc *>(cpu->user);
  if (a < POKEYS_BASE) {
    m->memory[GAMMA_RAM_OFS + (a & GAMMA_RAM_MASK)] = v;
    return;
  }
  if ((a & 0xe000) == EEPROM) {
    m->memory[EEPROM_OFS + (a & (EEPROM_SIZE - 1))] = v;
    return;
  }
  switch (a & 0xf800) {
    case POKEYS_BASE: m->pokey_write(pokey_chip(a), pokey_reg(a), v); break;
    case DSW2_IRQ_ACK:
      m->gamma.irq = 0;
      m->gamma_irq_clock = 0;
      break;
    case ALPHA_WRITE:
      m->alpha_rcvd = false;
      m->gamma_xmtd = true;
      m->gamma_data = v;
      break;
  }
  // 0x4800 coin counters: ignored
}

avg::Memory mhavoc::avg_memory() const {
  return avg::Memory{memory + VRAM_OFS, MH_VRAM_SIZE, vrom, MH_VRAM_SIZE, 0x1000,
                     0, 0, avg::MHAVOC, memory + COLOR_OFS, avgrom};
}

uint16_t mhavoc::decode_frame(const uint8_t *) {
  const uint16_t count = avg::decode(avg_memory(), avg_state, work, MAX_LINES);
  avg_done_at = cycles + avg_state.draw_clocks * AVG_CLOCKS_NUM / AVG_CLOCKS_DEN;
  return count;
}

uint16_t mhavoc::map_lines(uint16_t count) {
  return bwidow_layout::map(mhavoc_layout::SCREEN, work, count, 0);
}

#ifdef LED_PIN
void mhavoc::menuLeds(CRGB *leds) {
  fill_solid(leds, NUM_LEDS, CRGB::Yellow);
}

void mhavoc::gameLeds(CRGB *leds) {
  menuLeds(leds);
}
#endif
