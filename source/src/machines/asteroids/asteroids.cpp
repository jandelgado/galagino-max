#include "asteroids.h"
#include "asteroids_rom.h"
#include "asteroids_vrom.h"
#include "../../emulation/asteroids_sound.h"

// memory map (MAME asteroid_map), 15 bit address bus; VRAM 0x4000-0x47ff
// in dvgMachine
enum : uint16_t {
  RAM_END = 0x0400,
  PLAYER_RAM = 0x0200, // 0x0200-0x03ff, pages swapped by RAMSEL
  IN0 = 0x2000,        // 0x2000-0x2007, one input bit per address
  IN1 = 0x2400,        // 0x2400-0x2407
  DSW = 0x2800,        // 0x2800-0x2803, two DIP bits per address
  DVG_GO = 0x3000,
  OUTLATCH = 0x3200,
  EXPLODE = 0x3600,
  THUMP = 0x3a00,
  AUDIO_LATCH = 0x3c00, // 0x3c00-0x3c07, data bit 7
  NOISE_RESET = 0x3e00,
  VROM = 0x5000, // 0x5000-0x57ff
  ROM = 0x6800,  // 0x6800-0x7fff
  VROM_SIZE = 0x0800,
};

// DVG word address of the vector ROM
static const uint16_t DVG_ROM_BASE = 0x800;

// IN0/IN1 bits, active high; the read returns 0x80 when set, else 0x7f
enum : uint8_t {
  IN0_CLOCK_3K = 1,
  IN0_DVG_BUSY = 2, // never set: the DVG list is decoded instantly
  IN0_HYPERSPACE = 3,
  IN0_FIRE = 4,
  IN1_COIN = 0,
  IN1_START1 = 3,
  IN1_THRUST = 5,
  IN1_RIGHT = 6,
  IN1_LEFT = 7,
};
// zero page: players in the current game, 0 in attract (ROM 0x68de stores
// it when START1 is accepted)
static const uint16_t RAM_PLAYERS = 0x1c;

// START pressed: BUTTON_START with a COIN pin, else START reports as
// BUTTON_EXTRA (and a delayed virtual coin + start, see input.cpp)
static const unsigned int START_KEYS = BUTTON_START | BUTTON_EXTRA;

enum : uint8_t { OUTLATCH_LAMPS = 0x03, OUTLATCH_RAMSEL = 0x04, LATCH_DATA = 0x80 };

// DVG screen 1024 x 789 (y range of MAME's visible area), upright and
// scaled by 15/64 onto 240 columns x 185 rows, centered in the 288 rows of
// the panel
static const vec2d::Rect VISIBLE = {0, 118, 1023, 906};
static const int SCALE_MUL = 15, SCALE_SHIFT = 6;
static const int TOP_BORDER = 51;

// native x 0 -> panel column 0, native top (y 906) -> panel row 51
static int16_t panel_col(int16_t x) { return (x * SCALE_MUL) >> SCALE_SHIFT; }

static int16_t panel_row(int16_t y) {
  return TOP_BORDER + (((VISIBLE.y1 - y) * SCALE_MUL) >> SCALE_SHIFT);
}

// "(C) ATARI 1979" is a vector ROM subroutine (DVG word 0x852) starting with
// LABS y=128 x=400 scale 0. Scale 1 doubles it, glyphs included, for the
// small panel; x 310 keeps its center (width 180 -> 360). Patches the
// unpacked copy in Arena, idempotent.
// Doubled so it stays readable on the small display and we don't forget
// which nice company brought this game to us.
static void enlarge_copyright(unsigned char *vrom) {
  static const uint16_t LABS_W1 = (0x853 - 0x800) * 2; // x + global scale
  if (vrom[LABS_W1] == 0x90 && vrom[LABS_W1 + 1] == 0x01) { // x=400, scale 0
    vrom[LABS_W1] = 0x36;     // x=310
    vrom[LABS_W1 + 1] = 0x11; // scale 1
  }
}

asteroids::asteroids()
  : dvgMachine(main_read, main_write),
    asteroids_rom(asteroids_rom_blob),
    asteroids_vrom(asteroids_vrom_blob) {
  static_assert(RAM_END <= VRAM_OFS, "Asteroids RAM overlaps VRAM");
  rom = asteroids_rom.data();
  vrom = asteroids_vrom.data();
  enlarge_copyright(const_cast<unsigned char *>(vrom));
  dvg_mem = dvg::Memory{nullptr, vrom, DVG_ROM_BASE, VROM_SIZE / 2, 0, 0};
}

void asteroids::reset() {
  dvgMachine::reset();
  ramsel = false;
  lamps = 0;
}

uint8_t asteroids::in0(uint8_t bit) const {
  const unsigned int k = input->buttons_get();
  switch (bit) {
    case IN0_CLOCK_3K: return input_bit(clock_3k());
    case IN0_HYPERSPACE: return input_bit(in_game() && (k & START_KEYS));
    case IN0_FIRE: return input_bit(k & BUTTON_FIRE);
  }
  return input_bit(false); // DVG done, diag step, tilt, self test off
}

bool asteroids::in_game() const {
  return memory[RAM_PLAYERS] != 0;
}

// Stick as on the portrait panel, the game's controls are relative anyway.
// START is hyperspace in game, so coin and start only reach the game in
// attract mode: a hyperspace press must not insert a (virtual) coin.
uint8_t asteroids::in1(uint8_t bit) const {
  const unsigned int k = input->buttons_get();
  switch (bit) {
    case IN1_COIN: return input_bit(!in_game() && (k & BUTTON_COIN));
    case IN1_START1: return input_bit(!in_game() && (k & BUTTON_START));
    case IN1_THRUST: return input_bit(k & BUTTON_UP);
    case IN1_RIGHT: return input_bit(k & BUTTON_RIGHT);
    case IN1_LEFT: return input_bit(k & BUTTON_LEFT);
  }
  return input_bit(false);
}

uint8_t asteroids::main_read(m6502_t *cpu, uint16_t a) {
  asteroids *s = static_cast<asteroids *>(cpu->user);
  a &= ADDR_MASK;
  if (a >= ROM) {
    return s->rom[a - ROM];
  }
  if (a < RAM_END) {
    if (s->ramsel && a >= PLAYER_RAM) {
      a ^= 0x100;
    }
    return s->memory[a];
  }
  if (a >= VRAM && a < VRAM + VRAM_SIZE) {
    return s->memory[VRAM_OFS + a - VRAM];
  }
  if (a >= VROM && a < VROM + VROM_SIZE) {
    return s->vrom[a - VROM];
  }

  switch (a & ~7) {
    case IN0: return s->in0(a & 7);
    case IN1: return s->in1(a & 7);
    case DSW: return dsw_read(ASTEROIDS_DSW, a);
  }
  return 0;
}

void asteroids::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  asteroids *s = static_cast<asteroids *>(cpu->user);
  a &= ADDR_MASK;
  if (a < RAM_END) {
    if (s->ramsel && a >= PLAYER_RAM) {
      a ^= 0x100;
    }
    s->memory[a] = v;
    return;
  }
  if (a >= VRAM && a < VRAM + VRAM_SIZE) {
    s->memory[VRAM_OFS + a - VRAM] = v;
    return;
  }
  if ((a & ~7) == AUDIO_LATCH) {
    const uint8_t bit = 1 << (a & 7);
    if (v & LATCH_DATA) {
      s->soundregs[AST_SND_LATCH] |= bit;
    } else {
      s->soundregs[AST_SND_LATCH] &= ~bit;
    }
    return;
  }

  switch (a) {
    case DVG_GO: s->go(); break;
    case OUTLATCH:
      s->ramsel = v & OUTLATCH_RAMSEL;
      s->lamps = ~v & OUTLATCH_LAMPS;
      break;
    case EXPLODE: s->soundregs[AST_SND_EXPLODE] = v; break;
    case THUMP: s->soundregs[AST_SND_THUMP] = v; break;
    case NOISE_RESET: s->soundregs[AST_SND_NOISE_RESET]++; break;
  }
  // 0x3400 watchdog: ignored
}

// Clips lines to the visible area and turns them into panel coordinates,
// keeping only visible ones. Returns their count.
uint16_t asteroids::map_lines(uint16_t count) {
  uint16_t kept = 0;
  for (uint16_t i = 0; i < count; i++) {
    vec2d::Line l = work[i];
    if (!vec2d::clip(l, VISIBLE)) {
      continue;
    }

    vec2d::Line &o = work[kept++];
    o.x0 = panel_col(l.x0);
    o.y0 = panel_row(l.y0);
    o.x1 = panel_col(l.x1);
    o.y1 = panel_row(l.y1);
    o.color = l.color;
  }
  return kept;
}

#ifdef LED_PIN
// logo colors
static const CRGB ASTEROIDS_LED_RED(0x880f15), ASTEROIDS_LED_ORANGE(0xa85a0e),
    ASTEROIDS_LED_YELLOW(0xfff200);
static const uint8_t EXPLODE_VOLUME = 0x3c; // explode register bits 2-5

void asteroids::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      ASTEROIDS_LED_RED, ASTEROIDS_LED_ORANGE, ASTEROIDS_LED_YELLOW, ASTEROIDS_LED_YELLOW,
      ASTEROIDS_LED_YELLOW, ASTEROIDS_LED_ORANGE, ASTEROIDS_LED_RED};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// Explosions flash yellow, brightness following the explosion volume.
void asteroids::gameLeds(CRGB *leds) {
  menuLeds(leds);
  const uint8_t volume = (soundregs[AST_SND_EXPLODE] & EXPLODE_VOLUME) >> 2;
  if (!volume) {
    return;
  }
  const CRGB flash = CRGB(ASTEROIDS_LED_YELLOW).nscale8(volume * 17);
  fill_solid(leds, NUM_LEDS, flash);
}
#endif
