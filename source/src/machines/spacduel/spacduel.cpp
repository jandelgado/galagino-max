#include "spacduel.h"
#include "spacduel_rom.h"
#include "spacduel_vrom.h"
#include "../../emulation/pokey.h"

// memory map (MAME spacduel_map), full 16 bit bus. RAM at memory[0],
// VRAM in dvgMachine::memory at VRAM_OFS.
enum : uint16_t {
  RAM_END = 0x0400,
  IN0 = 0x0800,
  IN1 = 0x0900, // 0x0900-0x0907, see in1()
  EAROM_READ = 0x0a00,
  OUT = 0x0c00,
  AVG_GO = 0x0c80,
  AVG_RESET = 0x0d80,
  IRQ_ACK = 0x0e00,
  EAROM_CONTROL = 0x0e80,
  EAROM_WRITE = 0x0f00, // 0x0f00-0x0f3f, offset = EAROM address
  POKEY1 = 0x1000,      // 0x1000-0x13ff, POKEY 2 0x1400-0x17ff
  SD_VRAM = 0x2000,
  SD_VRAM_SIZE = 0x0800,
  SD_VROM = 0x2800, // 0x2800-0x3fff
  SD_VROM_SIZE = 0x1800,
  ROM = 0x4000, // 0x4000-0x8fff
  ROM_END = 0x9000,
  ROM_MIRRORED = 0x8000, // 0x9000-0xffff mirror 0x8000-0x8fff
};
// 0x0d00 watchdog: ignored

// IN0, active low unless noted
enum : uint8_t {
  IN0_COIN1 = 0x02,
  IN0_IDLE = 0x7f, // coins, service, diag step, AVG done (high: decoded at GO)
  IN0_CLOCK_3K = 0x80,
};
// game RAM: select state, see spacduel_inputs.h
static const uint16_t RAM_SELECT = 0x03ee;
static const uint8_t SELECT_PENDING = 0x80;
// 0x0c00 lamps, active low
enum : uint8_t { LAMP_START = 0x20 };

// POKEY registers beyond AUDF1..AUDC4 (0-7)
enum : uint8_t { POKEY_AUDCTL = 0x08, POKEY_ALLPOT = 0x08, POKEY_RANDOM = 0x0a, POKEY_SKCTL = 0x0f };
enum : uint8_t { SK_RESET = 0x03, AUDCTL_POLY9 = 0x80 };

// AVG screen (MAME visarea 0..540 x 0..400), beam center
static const int32_t XCENTER = 270 << 16, YCENTER = 200 << 16;
// AVG byte address of the vector ROM
static const uint16_t AVG_ROM_BASE = SD_VROM - SD_VRAM;

spacduel::spacduel()
  : dvgMachine(main_read, main_write, IRQ),
    spacduel_rom(spacduel_rom_blob),
    spacduel_vrom(spacduel_vrom_blob) {
  static_assert(RAM_END <= VRAM_OFS, "Space Duel RAM overlaps VRAM");
  static_assert(SD_VRAM_SIZE <= MAX_VRAM_SIZE, "Space Duel VRAM exceeds MAX_VRAM_SIZE");
  rom = spacduel_rom.data();
  vrom = spacduel_vrom.data();
  spacduel_layout::palette(pal);
}

void spacduel::reset() {
  dvgMachine::reset();
  earom.reset();
  avg_state.reset(XCENTER, YCENTER);
  out = 0xff;
  for (uint8_t n = 0; n < POKEYS; n++) {
    allpot[n] = 0;
    random_base[n] = 0;
  }
}

uint8_t spacduel::in0() const {
  uint8_t v = IN0_IDLE;
  if (input->buttons_get() & BUTTON_COIN) {
    v &= ~IN0_COIN1;
  }
  return v | (clock_3k() ? IN0_CLOCK_3K : 0);
}

uint8_t spacduel::in1(uint8_t offset) const {
  const SdSelect select = (memory[RAM_SELECT] & SELECT_PENDING) ? SD_SELECT_PENDING : SD_SELECTED;
  return spacduel_in1(offset, input->buttons_get(), select);
}

bool spacduel::pokey_running(uint8_t n) const {
  return soundregs[n * Pokey::REG_COUNT + Pokey::REG_SKCTL] & SK_RESET;
}

// ALLPOT: MAME allpot_r callback (DIP switches) while running, else the
// last value
uint8_t spacduel::pokey_read(uint8_t n, uint8_t reg) {
  if (reg == POKEY_ALLPOT) {
    if (pokey_running(n)) {
      allpot[n] = n ? SPACDUEL_DSW1 : SPACDUEL_DSW0;
    }
    return allpot[n];
  }
  if (reg == POKEY_RANDOM) {
    const bool poly9 = soundregs[n * Pokey::REG_COUNT + Pokey::REG_AUDCTL] & AUDCTL_POLY9;
    return Pokey::random(pokey_running(n) ? cpu_cycles() - random_base[n] : 0, poly9);
  }
  return 0;
}

void spacduel::pokey_write(uint8_t n, uint8_t reg, uint8_t v) {
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

uint8_t spacduel::main_read(m6502_t *cpu, uint16_t a) {
  spacduel *s = static_cast<spacduel *>(cpu->user);
  if (a >= ROM_END) {
    return s->rom[ROM_MIRRORED - ROM + (a & 0x0fff)];
  }
  if (a >= ROM) {
    return s->rom[a - ROM];
  }
  if (a < RAM_END) {
    return s->memory[a];
  }
  if (a >= SD_VRAM && a < SD_VRAM + SD_VRAM_SIZE) {
    return s->memory[VRAM_OFS + a - SD_VRAM];
  }
  if (a >= SD_VROM) {
    return s->vrom[a - SD_VROM];
  }
  if ((a & 0xf800) == POKEY1) {
    return s->pokey_read((a >> 10) & 1, a & 0x0f);
  }
  if ((a & ~7) == IN1) {
    return s->in1(a & 7);
  }
  switch (a) {
    case IN0: return s->in0();
    case EAROM_READ: return s->earom.read();
  }
  return 0;
}

// Er2055::control() takes the Centipede wiring (C1 = !b1, C2 = b2);
// Space Duel has C1 = !b2, C2 = b1
static uint8_t earom_wiring(uint8_t v) {
  return (v & ~0x06) | ((v & 0x02) << 1) | ((v & 0x04) >> 1);
}

void spacduel::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  spacduel *s = static_cast<spacduel *>(cpu->user);
  if (a < RAM_END) {
    s->memory[a] = v;
    return;
  }
  if (a >= SD_VRAM && a < SD_VRAM + SD_VRAM_SIZE) {
    s->memory[VRAM_OFS + a - SD_VRAM] = v;
    return;
  }
  if ((a & ~0x3f) == EAROM_WRITE) {
    s->earom.latch(a & 0x3f, v);
    return;
  }
  if ((a & 0xf800) == POKEY1) {
    s->pokey_write((a >> 10) & 1, a & 0x0f, v);
    return;
  }
  switch (a) {
    case OUT: s->out = v; break;
    case AVG_GO: s->go(); break;
    case IRQ_ACK: s->irq_ack(); break;
    case AVG_RESET:
      // MAME avg_vgrst
      s->avg_state.scale = s->avg_state.bin_scale = s->avg_state.color = 0;
      break;
    case EAROM_CONTROL: s->earom.control(earom_wiring(v)); break;
  }
}

uint16_t spacduel::decode_frame(const uint8_t *vram) {
  const avg::Memory m = {vram, SD_VRAM_SIZE, vrom, AVG_ROM_BASE, SD_VROM_SIZE, 0, 0, avg::COLOR, nullptr};
  return avg::decode(m, avg_state, work, MAX_LINES);
}

// 0x0c00 bits 6/7 low = flipped (MAME set_flip_x(BIT(~data, 6)))
uint16_t spacduel::map_lines(uint16_t count) {
  return spacduel_layout::map(work, count, ~out & (spacduel_layout::FLIP_X | spacduel_layout::FLIP_Y));
}

#ifdef LED_PIN
// logo colors: glow blue, cyan, magenta
static const CRGB SPACDUEL_LED_BLUE(0x012c46), SPACDUEL_LED_CYAN(0x28c7d7);
static const CRGB SPACDUEL_LED_MAGENTA(0x9c3db3);
static const uint8_t LED_MIDDLE = NUM_LEDS / 2;

void spacduel::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      SPACDUEL_LED_BLUE, SPACDUEL_LED_CYAN, SPACDUEL_LED_MAGENTA, SPACDUEL_LED_MAGENTA,
      SPACDUEL_LED_MAGENTA, SPACDUEL_LED_CYAN, SPACDUEL_LED_BLUE};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// the start lamp (blinking in attract) lights the middle LED white
void spacduel::gameLeds(CRGB *leds) {
  menuLeds(leds);
  if (!(out & LAMP_START)) {
    leds[LED_MIDDLE] = CRGB::White;
  }
}
#endif
