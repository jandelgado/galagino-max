#include "tempest.h"
#include "tempest_rom.h"
#include "tempest_vrom.h"
#include "../../emulation/pokey.h"

// memory map (MAME tempest main_map), full 16 bit bus. VRAM, color RAM and
// program RAM live in dvgMachine::memory.
enum : uint16_t {
  RAM_END = 0x0800,
  COLORRAM = 0x0800, // 0x0800-0x080f
  IN0 = 0x0c00,
  DSW1 = 0x0d00,
  DSW2 = 0x0e00,
  TP_VRAM = 0x2000,
  TP_VRAM_SIZE = 0x1000,
  TP_VROM = 0x3000, // 0x3000-0x3fff
  TP_VROM_SIZE = 0x1000,
  VIDEO_INVERT = 0x4000,
  AVG_GO = 0x4800,
  IRQ_ACK = 0x5000, // also the watchdog
  AVG_RESET = 0x5800,
  EAROM_WRITE = 0x6000, // 0x6000-0x603f, offset = EAROM address
  EAROM_CONTROL = 0x6040,
  MB_STATUS = 0x6040,
  EAROM_READ = 0x6050,
  MB_LO = 0x6060,
  MB_HI = 0x6070,
  MB_GO = 0x6080, // 0x6080-0x609f, offset = command
  POKEY_BASE = 0x60c0, // POKEY 1 0x60c0-0x60cf, POKEY 2 0x60d0-0x60df
  LAMPS = 0x60e0,
  ROM = 0x9000, // 0x9000-0xdfff
  ROM_END = 0xe000,
  ROM_MIRROR = 0xf000, // 0xf000-0xffff = 0xd000-0xdfff (vectors)
  ROM_MIRRORED = 0xd000,
};

// IN0, active low unless noted
enum : uint8_t {
  IN0_COIN1 = 0x04, // left coin
  IN0_IDLE = 0x3f,  // coins, tilt, service, diag step
  IN0_AVG_HALTED = 0x40,
  IN0_CLOCK_3K = 0x80,
};
// IN1 (POKEY 1 pots): spinner bits 0-3, upright cabinet, unused bits high
enum : uint8_t { IN1_SPINNER = 0x0f, IN1_IDLE = 0xf0 };
// IN2 (POKEY 2 pots), buttons active low
enum : uint8_t { IN2_SUPERZAP = 0x08, IN2_FIRE = 0x10, IN2_START1 = 0x20, IN2_IDLE = 0xf8 };
// lamps at 0x60e0, active low
enum : uint8_t { LAMP_START1 = 0x02 };

// POKEY registers beyond AUDF1..AUDC4 (0-7)
enum : uint8_t { POKEY_AUDCTL = 0x08, POKEY_ALLPOT = 0x08, POKEY_RANDOM = 0x0a, POKEY_SKCTL = 0x0f };
enum : uint8_t { SK_RESET = 0x03, AUDCTL_POLY9 = 0x80 };

// AVG opcodes the halt check needs (avg.cpp)
enum : uint8_t { OP_HALT = 1, OP_JMPL = 7 };

// AVG screen (MAME visarea 0..580 x 0..570), beam center
static const int32_t XCENTER = 290 << 16, YCENTER = 285 << 16;
// AVG byte address of the vector ROM
static const uint16_t AVG_ROM_BASE = TP_VROM - TP_VRAM;

tempest::tempest()
  : dvgMachine(main_read, main_write, IRQ),
    tempest_rom(tempest_rom_blob),
    tempest_vrom(tempest_vrom_blob) {
  static_assert(RAM_OFS + RAM_END <= RAMSIZE, "Tempest RAM does not fit");
  static_assert(TP_VRAM_SIZE <= MAX_VRAM_SIZE, "Tempest VRAM exceeds MAX_VRAM_SIZE");
  rom = tempest_rom.data();
  vrom = tempest_vrom.data();
  tempest_layout::palette(pal);
}

void tempest::reset() {
  dvgMachine::reset();
  mathbox.reset();
  earom.reset();
  spinner = Spinner();
  avg_state.reset(XCENTER, YCENTER);
  avg_running = false;
  flip = 0;
  lamps = 0xff;
  for (uint8_t n = 0; n < POKEYS; n++) {
    allpot[n] = 0;
    random_base[n] = 0;
  }
}

// The AVG list is only valid after the first GO; decoding before would
// draw whatever the zeroed VRAM and the vector ROM spell.
void tempest::run_frame() {
  dvgMachine::run_frame();
  if (avg_running) {
    go();
  }
}

void tempest::frame_input() {
  const unsigned int k = input->buttons_get();
  spinner.step((k & BUTTON_RIGHT) ? 1 : (k & BUTTON_LEFT) ? -1 : 0);
}

// The AVG halts when the list head jumps to a HALT (the game parks it
// there) and after an AVG reset without GO. The IRQ handler restarts it.
bool tempest::avg_halted() const {
  if (!avg_running) {
    return true;
  }

  const uint8_t *vram = memory + VRAM_OFS;
  const uint8_t head = vram[1] >> 5;
  if (head != OP_JMPL) {
    return head == OP_HALT;
  }

  const uint16_t target = (((vram[1] & 0x1f) << 8) | vram[0]) << 1;
  if (target + 1 < TP_VRAM_SIZE) {
    return (vram[target + 1] >> 5) == OP_HALT;
  }
  if (target >= AVG_ROM_BASE && target + 1 < AVG_ROM_BASE + TP_VROM_SIZE) {
    return (vrom[target + 1 - AVG_ROM_BASE] >> 5) == OP_HALT;
  }
  return false;
}

uint8_t tempest::in0() const {
  uint8_t v = IN0_IDLE;
  if (input->buttons_get() & BUTTON_COIN) {
    v &= ~IN0_COIN1;
  }
  return v | (avg_halted() ? IN0_AVG_HALTED : 0) | (clock_3k() ? IN0_CLOCK_3K : 0);
}

uint8_t tempest::in1() const {
  return IN1_IDLE | (spinner.count() & IN1_SPINNER);
}

uint8_t tempest::in2() const {
  const unsigned int k = input->buttons_get();
  uint8_t v = IN2_IDLE | TEMPEST_IN2_DIPS;
  if (k & (BUTTON_FIRE | BUTTON_A)) {
    v &= ~IN2_FIRE;
  }
  if (k & (BUTTON_UP | BUTTON_DOWN | BUTTON_B)) {
    v &= ~IN2_SUPERZAP;
  }
  if (k & BUTTON_START) {
    v &= ~IN2_START1;
  }
  return v;
}

bool tempest::pokey_running(uint8_t n) const {
  return soundregs[n * Pokey::REG_COUNT + Pokey::REG_SKCTL] & SK_RESET;
}

// ALLPOT: MAME pot_r gives 0 (done at once) for a set port bit, so after
// the POTGO the game writes before every read, ALLPOT is the inverted port
uint8_t tempest::pokey_read(uint8_t n, uint8_t reg) {
  if (reg == POKEY_ALLPOT) {
    if (pokey_running(n)) {
      allpot[n] = ~(n ? in2() : in1());
    }
    return allpot[n];
  }
  if (reg == POKEY_RANDOM) {
    const bool poly9 = soundregs[n * Pokey::REG_COUNT + Pokey::REG_AUDCTL] & AUDCTL_POLY9;
    return Pokey::random(pokey_running(n) ? cpu_cycles() - random_base[n] : 0, poly9);
  }
  return 0;
}

void tempest::pokey_write(uint8_t n, uint8_t reg, uint8_t v) {
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

uint8_t tempest::main_read(m6502_t *cpu, uint16_t a) {
  tempest *s = static_cast<tempest *>(cpu->user);
  if (a >= ROM_MIRROR) {
    return s->rom[a - ROM_MIRROR + ROM_MIRRORED - ROM];
  }
  if (a >= ROM && a < ROM_END) {
    return s->rom[a - ROM];
  }
  if (a < RAM_END) {
    return s->memory[RAM_OFS + a];
  }
  if (a >= TP_VRAM && a < TP_VRAM + TP_VRAM_SIZE) {
    return s->memory[VRAM_OFS + a - TP_VRAM];
  }
  if (a >= TP_VROM && a < TP_VROM + TP_VROM_SIZE) {
    return s->vrom[a - TP_VROM];
  }
  if ((a & ~0x1f) == POKEY_BASE) {
    return s->pokey_read((a >> 4) & 1, a & 0x0f);
  }
  switch (a) {
    case IN0: return s->in0();
    case DSW1: return TEMPEST_DSW1;
    case DSW2: return TEMPEST_DSW2;
    case MB_STATUS: return 0; // done
    case EAROM_READ: return s->earom.read();
    case MB_LO: return s->mathbox.lo();
    case MB_HI: return s->mathbox.hi();
  }
  return 0;
}

// Er2055::control() takes the Centipede wiring (C1 = !b1, C2 = b2);
// Tempest has C1 = !b2, C2 = b1
static uint8_t earom_wiring(uint8_t v) {
  return (v & ~0x06) | ((v & 0x02) << 1) | ((v & 0x04) >> 1);
}

void tempest::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  tempest *s = static_cast<tempest *>(cpu->user);
  if (a < RAM_END) {
    s->memory[RAM_OFS + a] = v;
    return;
  }
  if (a >= TP_VRAM && a < TP_VRAM + TP_VRAM_SIZE) {
    s->memory[VRAM_OFS + a - TP_VRAM] = v;
    return;
  }
  if (a >= COLORRAM && a < COLORRAM + COLOR_SIZE) {
    s->memory[COLOR_OFS + a - COLORRAM] = v;
    return;
  }
  if ((a & ~0x3f) == EAROM_WRITE) {
    s->earom.latch(a & 0x3f, v);
    return;
  }
  if ((a & ~0x1f) == MB_GO) {
    s->mathbox.go(a & 0x1f, v);
    return;
  }
  if ((a & ~0x1f) == POKEY_BASE) {
    s->pokey_write((a >> 4) & 1, a & 0x0f, v);
    return;
  }
  switch (a) {
    case VIDEO_INVERT: s->flip = v & (tempest_layout::FLIP_X | tempest_layout::FLIP_Y); break;
    case AVG_GO: s->avg_running = true; break;
    case IRQ_ACK: s->irq_ack(); break;
    case AVG_RESET:
      // MAME avg_vgrst
      s->avg_running = false;
      s->avg_state.scale = s->avg_state.bin_scale = s->avg_state.color = 0;
      break;
    case EAROM_CONTROL: s->earom.control(earom_wiring(v)); break;
    case LAMPS: s->lamps = v; break;
  }
}

uint16_t tempest::decode_frame(const uint8_t *vram) {
  const avg::Memory m = {vram, TP_VRAM_SIZE, vrom, AVG_ROM_BASE, TP_VROM_SIZE, 0, 0, avg::TEMPEST,
                         memory + COLOR_OFS};
  return avg::decode(m, avg_state, work, MAX_LINES);
}

uint16_t tempest::map_lines(uint16_t count) {
  return tempest_layout::map(work, count, flip);
}

#ifdef LED_PIN
// logo colors: dark red, red, orange, yellow
static const CRGB TEMPEST_LED_DARK(0xae241f), TEMPEST_LED_RED(0xc61219);
static const CRGB TEMPEST_LED_ORANGE(0xe6521f), TEMPEST_LED_YELLOW(0xf59c12);
static const uint8_t LED_MIDDLE = NUM_LEDS / 2;

void tempest::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      TEMPEST_LED_DARK, TEMPEST_LED_RED, TEMPEST_LED_ORANGE, TEMPEST_LED_YELLOW,
      TEMPEST_LED_ORANGE, TEMPEST_LED_RED, TEMPEST_LED_DARK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// the start button lamp (blinking in attract) lights the middle LED white
void tempest::gameLeds(CRGB *leds) {
  menuLeds(leds);
  if (!(lamps & LAMP_START1)) {
    leds[LED_MIDDLE] = CRGB::White;
  }
}
#endif
