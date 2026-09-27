#include "asteroids.h"
#include "asteroids_rom.h"
#include "asteroids_vrom.h"
#include "../../emulation/arena.h"
#include "../../emulation/asteroids_sound.h"
#include "../../emulation/dvg.h"

// Experimental: dim halo around each line, like a vector monitor's beam
// bloom. Costs 480 bytes of Arena and some render time.
#define ASTEROIDS_GLOW

// memory map (MAME asteroid_map), 15 bit address bus
enum : uint16_t {
  ADDR_MASK = 0x7fff,
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
  VRAM = 0x4000, // 0x4000-0x47ff
  VROM = 0x5000, // 0x5000-0x57ff
  ROM = 0x6800,  // 0x6800-0x7fff
  VRAM_SIZE = 0x0800,
  VROM_SIZE = 0x0800,
};

// vector RAM lives in machineBase::memory right after work RAM
static const uint16_t VRAM_OFS = RAM_END;
static_assert(VRAM_OFS + VRAM_SIZE <= RAMSIZE, "Asteroids RAM does not fit");

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
static const uint32_t CLOCK_3K_BIT = 0x100; // MAME clock_r: total cycles bit 8

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

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t rgb = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (rgb >> 8) | (rgb << 8);
}

static uint16_t grey565(uint8_t level) {
  return rgb565(level, level, level);
}

// half the line's level, tinted blue like a vector monitor's phosphor bloom
static uint16_t glow565(uint8_t level) {
  const uint8_t l = level / 2;
  return rgb565(l * 3 / 4, l * 7 / 8, l * 3 / 2 > 255 ? 255 : l * 3 / 2);
}

asteroids::asteroids() {
  memset(&m_cpu, 0, sizeof(m_cpu));
  m_cpu.read = main_read;
  m_cpu.write = main_write;
  m_cpu.user = this;
  rom = asteroids_rom.data();
  vrom = asteroids_vrom.data();

  // DVG intensity 0-15 as grey; the game uses mostly 7 (objects) and 15
  // (bullets, text), both must stand out on the small LCD
  palette[0] = glow[0] = 0;
  for (uint8_t i = 1; i < 16; i++) {
    palette[i] = grey565(80 + i * 11);
    glow[i] = glow565(80 + i * 11);
  }
}

asteroids::~asteroids() {
  asteroids_rom.release();
  asteroids_vrom.release();
}

void asteroids::start() {
  if (lines) {
    return; // start() runs twice
  }

  lines = Arena::alloc<vec2d::Line>(MAX_LINES);
  line_next = Arena::alloc<uint16_t>(MAX_LINES);
  line_active = Arena::alloc<uint16_t>(MAX_LINES);
  strip_head = Arena::alloc<uint16_t>(STRIPS);
#ifdef ASTEROIDS_GLOW
  glow_carry = Arena::alloc<uint16_t>(renderWidth());
#endif
  raster.init(line_next, line_active, strip_head, STRIPS, glow_carry);
  raster.begin(lines, 0);
}

void asteroids::reset() {
  machineBase::reset();
  cycles = 0;
  next_nmi = CYCLES_PER_NMI;
  ramsel = false;
  lamps = 0;
  m6502_reset(&m_cpu);
}

static uint8_t input_bit(bool set) {
  return set ? 0x80 : 0x7f;
}

uint8_t asteroids::in0(uint8_t bit) const {
  const unsigned int k = input->buttons_get();
  switch (bit) {
    case IN0_CLOCK_3K: return input_bit(cycles & CLOCK_3K_BIT);
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
    case DSW: {
      // LS153: address n selects DIP bits 7-2n (-> D1) and 6-2n (-> D0)
      const uint8_t shift = 6 - 2 * (a & 3);
      return 0xfc | ((ASTEROIDS_DSW >> shift) & 3);
    }
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
    case DVG_GO:
      s->go_latch.publish([s](VideoState &vs) {
        memcpy(vs.vram, s->memory + VRAM_OFS, sizeof(vs.vram));
      });
      s->game_started = 1;
      break;
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

void asteroids::run_frame() {
  // NMI every 6144 cycles; the self test switch that masks it is never on
  const uint32_t end = cycles + CYCLES_PER_FRAME;
  while ((int32_t)(cycles - end) < 0) {
    if ((int32_t)(cycles - next_nmi) >= 0) {
      m_cpu.nmi = 1;
      next_nmi += CYCLES_PER_NMI;
    }
    cycles += m6502_step(&m_cpu);
  }
}

// Clips lines to the visible area and turns them into panel coordinates,
// keeping only visible ones. Returns their count.
uint16_t asteroids::map_lines(uint16_t count) {
  uint16_t kept = 0;
  for (uint16_t i = 0; i < count; i++) {
    vec2d::Line l = lines[i];
    if (!vec2d::clip(l, VISIBLE)) {
      continue;
    }

    vec2d::Line &o = lines[kept++];
    o.x0 = panel_col(l.x0);
    o.y0 = panel_row(l.y0);
    o.x1 = panel_col(l.x1);
    o.y1 = panel_row(l.y1);
    o.color = l.color;
  }
  return kept;
}

void asteroids::prepare_frame() {
  if (!game_started) {
    raster.begin(lines, 0); // no display list before the first GO
    return;
  }
  go_latch.read(video);
  const uint16_t n = dvg::decode(video.vram, vrom, lines, MAX_LINES);
  raster.begin(lines, map_lines(n));
}

// frame_buffer is cleared by the caller
void asteroids::render_row(short row) {
#ifdef ASTEROIDS_GLOW
  raster.render(row, frame_buffer, 240, palette, glow);
#else
  raster.render(row, frame_buffer, 240, palette);
#endif
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
