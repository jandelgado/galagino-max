#include "gravitar.h"
#include "gravitar_rom.h"
#include "gravitar_vrom.h"

// memory map (MAME bwidow_map), full 16 bit bus
enum : uint16_t {
  RAM_LOW_END = 0x0400,
  RAM_END = 0x0800,
  GV_VRAM = 0x2000,
  GV_VRAM_SIZE = 0x0800,
  GV_VROM = 0x2800, // 0x2800-0x5fff
  GV_VROM_SIZE = 0x3800,
  POKEY1 = 0x6000, // 0x6000-0x67ff, POKEY 2 0x6800-0x6fff
  EAROM_READ = 0x7000,
  IN0 = 0x7800,
  IN3 = 0x8000,
  IN4 = 0x8800,
  MISC = 0x8800, // write
  AVG_GO = 0x8840,
  AVG_RESET = 0x8880,
  IRQ_ACK = 0x88c0,
  EAROM_CONTROL = 0x8900,
  EAROM_WRITE = 0x8940, // 0x8940-0x897f, offset = EAROM address
  ROM = 0x9000,         // 0x9000-0xefff
  ROM_RELOAD = 0xf000,  // 0xf000-0xffff repeats 0xe000-0xefff (vectors)
};
// 0x8980-0x89ed watchdog: ignored

// IN0, active low unless noted
enum : uint8_t {
  IN0_COIN1 = 0x02,
  IN0_IDLE = 0x3f, // coins, service, diag step
  IN0_AVG_DONE = 0x40,
  IN0_CLOCK_3K = 0x80,
};
// 0x8800 write, active low
enum : uint8_t { LAMP_START = 0x10 };

gravitar::gravitar()
  : bwidowMachine(main_read, main_write, {GV_VRAM, GV_VRAM_SIZE, GV_VROM, GV_VROM_SIZE},
                  gravitar_layout::SCREEN, GRAVITAR_DSW0, GRAVITAR_DSW1),
    gravitar_rom(gravitar_rom_blob),
    gravitar_vrom(gravitar_vrom_blob) {
  static_assert(RAM_HIGH_OFS + RAM_END - RAM_LOW_END <= RAMSIZE, "Gravitar RAM does not fit");
  static_assert(GV_VRAM_SIZE <= MAX_VRAM_SIZE, "Gravitar VRAM exceeds MAX_VRAM_SIZE");
  rom = gravitar_rom.data();
  vrom = gravitar_vrom.data();
}

void gravitar::reset() {
  bwidowMachine::reset();
  misc = 0xff;
}

uint8_t gravitar::in0() const {
  uint8_t v = IN0_IDLE;
  if (input->buttons_get() & BUTTON_COIN) {
    v &= ~IN0_COIN1;
  }
  return v | (avg_done() ? IN0_AVG_DONE : 0) | (clock_3k() ? IN0_CLOCK_3K : 0);
}

uint8_t *gravitar::ram(uint16_t a) {
  return a < RAM_LOW_END ? memory + a : memory + RAM_HIGH_OFS + (a - RAM_LOW_END);
}

uint8_t gravitar::main_read(m6502_t *cpu, uint16_t a) {
  gravitar *g = static_cast<gravitar *>(cpu->user);
  if (a >= ROM_RELOAD) {
    return g->rom[ROM_RELOAD - 0x1000 - ROM + (a & 0x0fff)];
  }
  if (a >= ROM) {
    return g->rom[a - ROM];
  }
  if (a < RAM_END) {
    return *g->ram(a);
  }
  if (a >= GV_VRAM && a < GV_VRAM + GV_VRAM_SIZE) {
    return g->memory[VRAM_OFS + a - GV_VRAM];
  }
  if (a >= GV_VROM && a < GV_VROM + GV_VROM_SIZE) {
    return g->vrom[a - GV_VROM];
  }
  if ((a & 0xf000) == POKEY1) {
    return g->pokey_read((a >> 11) & 1, a & 0x0f);
  }
  switch (a) {
    case EAROM_READ: return g->earom.read();
    case IN0: return g->in0();
    case IN3: return gravitar_in3(g->input->buttons_get());
    case IN4: return gravitar_in4(g->input->buttons_get());
  }
  return 0;
}

void gravitar::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  gravitar *g = static_cast<gravitar *>(cpu->user);
  if (a < RAM_END) {
    *g->ram(a) = v;
    return;
  }
  if (a >= GV_VRAM && a < GV_VRAM + GV_VRAM_SIZE) {
    g->memory[VRAM_OFS + a - GV_VRAM] = v;
    return;
  }
  if ((a & 0xf000) == POKEY1) {
    g->pokey_write((a >> 11) & 1, a & 0x0f, v);
    return;
  }
  if ((a & ~0x3f) == EAROM_WRITE) {
    g->earom.latch(a & 0x3f, v);
    return;
  }
  switch (a) {
    case MISC: g->misc = v; break;
    case AVG_GO: g->go(); break;
    case AVG_RESET: g->avg_reset(); break;
    case IRQ_ACK: g->irq_ack(); break;
    case EAROM_CONTROL: g->earom_control(v); break;
  }
}

#ifdef LED_PIN
// logo colors: planet blue, red, orange
static const CRGB GRAVITAR_LED_BLUE(0x0063d1), GRAVITAR_LED_RED(0xcf2100);
static const CRGB GRAVITAR_LED_ORANGE(0xffaa28);
static const uint8_t LED_MIDDLE = NUM_LEDS / 2;

void gravitar::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      GRAVITAR_LED_BLUE, GRAVITAR_LED_RED, GRAVITAR_LED_ORANGE, GRAVITAR_LED_ORANGE,
      GRAVITAR_LED_ORANGE, GRAVITAR_LED_RED, GRAVITAR_LED_BLUE};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// the start lamp (blinking in attract) lights the middle LED white
void gravitar::gameLeds(CRGB *leds) {
  menuLeds(leds);
  if (!(misc & LAMP_START)) {
    leds[LED_MIDDLE] = CRGB::White;
  }
}
#endif
