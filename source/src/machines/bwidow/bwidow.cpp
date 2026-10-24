#include "bwidow.h"
#include "bwidow_rom.h"
#include "bwidow_vrom.h"

// memory map (MAME bwidow_map), full 16 bit bus
enum : uint16_t {
  RAM_LOW_END = 0x0400,
  RAM_END = 0x0800,
  BW_VRAM = 0x2000,
  BW_VRAM_SIZE = 0x0800,
  BW_VROM = 0x2800, // 0x2800-0x5fff
  BW_VROM_SIZE = 0x3800,
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

bwidow::bwidow()
  : bwidowMachine(main_read, main_write, {BW_VRAM, BW_VRAM_SIZE, BW_VROM, BW_VROM_SIZE},
                  bwidow_game_layout::SCREEN, BWIDOW_DSW0, BWIDOW_DSW1),
    bwidow_rom(bwidow_rom_blob),
    bwidow_vrom(bwidow_vrom_blob) {
  static_assert(RAM_HIGH_OFS + RAM_END - RAM_LOW_END <= RAMSIZE, "Black Widow RAM does not fit");
  static_assert(BW_VRAM_SIZE <= MAX_VRAM_SIZE, "Black Widow VRAM exceeds MAX_VRAM_SIZE");
  rom = bwidow_rom.data();
  vrom = bwidow_vrom.data();
}

void bwidow::reset() {
  bwidowMachine::reset();
  misc = 0xff;
  sticks = TwinStick();
  stick_state = 0;
}

uint8_t bwidow::in0() const {
  uint8_t v = IN0_IDLE;
  if (input->buttons_get() & BUTTON_COIN) {
    v &= ~IN0_COIN1;
  }
  return v | (avg_done() ? IN0_AVG_DONE : 0) | (clock_3k() ? IN0_CLOCK_3K : 0);
}

uint8_t *bwidow::ram(uint16_t a) {
  return a < RAM_LOW_END ? memory + a : memory + RAM_HIGH_OFS + (a - RAM_LOW_END);
}

uint8_t bwidow::main_read(m6502_t *cpu, uint16_t a) {
  bwidow *b = static_cast<bwidow *>(cpu->user);
  if (a >= ROM_RELOAD) {
    return b->rom[ROM_RELOAD - 0x1000 - ROM + (a & 0x0fff)];
  }
  if (a >= ROM) {
    return b->rom[a - ROM];
  }
  if (a < RAM_END) {
    return *b->ram(a);
  }
  if (a >= BW_VRAM && a < BW_VRAM + BW_VRAM_SIZE) {
    return b->memory[VRAM_OFS + a - BW_VRAM];
  }
  if (a >= BW_VROM && a < BW_VROM + BW_VROM_SIZE) {
    return b->vrom[a - BW_VROM];
  }
  if ((a & 0xf000) == POKEY1) {
    return b->pokey_read((a >> 11) & 1, a & 0x0f);
  }
  switch (a) {
    case EAROM_READ: return b->earom.read();
    case IN0: return b->in0();
    // the move stick read updates both sticks; IN4 reuses that state
    case IN3:
      b->stick_state = b->sticks.update(b->input->buttons_get());
      return bwidow_in3(b->stick_state);
    case IN4: return bwidow_in4(b->stick_state, b->input->buttons_get());
  }
  return 0;
}

void bwidow::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  bwidow *b = static_cast<bwidow *>(cpu->user);
  if (a < RAM_END) {
    *b->ram(a) = v;
    return;
  }
  if (a >= BW_VRAM && a < BW_VRAM + BW_VRAM_SIZE) {
    b->memory[VRAM_OFS + a - BW_VRAM] = v;
    return;
  }
  if ((a & 0xf000) == POKEY1) {
    b->pokey_write((a >> 11) & 1, a & 0x0f, v);
    return;
  }
  if ((a & ~0x3f) == EAROM_WRITE) {
    b->earom.latch(a & 0x3f, v);
    return;
  }
  switch (a) {
    case MISC: b->misc = v; break;
    case AVG_GO: b->go(); break;
    case AVG_RESET: b->avg_reset(); break;
    case IRQ_ACK: b->irq_ack(); break;
    case EAROM_CONTROL: b->earom_control(v); break;
  }
}

#ifdef LED_PIN
// logo colors: bright, mid and dark red
static const CRGB BWIDOW_LED_BRIGHT(0xff5f5f), BWIDOW_LED_MID(0xaa1818);
static const CRGB BWIDOW_LED_DARK(0x660d0d);
static const uint8_t LED_MIDDLE = NUM_LEDS / 2;

void bwidow::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      BWIDOW_LED_DARK, BWIDOW_LED_MID, BWIDOW_LED_BRIGHT, BWIDOW_LED_BRIGHT,
      BWIDOW_LED_BRIGHT, BWIDOW_LED_MID, BWIDOW_LED_DARK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// the start lamp (blinking in attract) lights the middle LED white
void bwidow::gameLeds(CRGB *leds) {
  menuLeds(leds);
  if (!(misc & LAMP_START)) {
    leds[LED_MIDDLE] = CRGB::White;
  }
}
#endif
