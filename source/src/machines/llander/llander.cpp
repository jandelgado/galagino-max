#include "llander.h"
#include "llander_rom.h"
#include "llander_vrom.h"
#include "llander_layout.h"
#include "../../emulation/llander_sound.h"

// memory map (MAME llander_map), 15 bit address bus; VRAM 0x4000-0x47ff
// in dvgMachine
enum : uint16_t {
  RAM_MIRROR_END = 0x2000, // 256 bytes mirrored over 0x0000-0x1fff
  RAM_MASK = 0x00ff,
  IN0 = 0x2000,
  IN1 = 0x2400, // 0x2400-0x2407, one input bit per address
  DSW = 0x2800, // 0x2800-0x2803, two DIP bits per address
  THRUST = 0x2c00,
  DVG_GO = 0x3000,
  OUTLATCH = 0x3200,
  SOUND = 0x3c00,
  NOISE_RESET = 0x3e00,
  VROM = 0x4800, // 0x4800-0x5fff
  VROM_SIZE = 0x1800,
  ROM = 0x6000, // 0x6000-0x7fff
};

// DVG word addresses of the vector ROM and its text glyphs
static const uint16_t DVG_ROM_BASE = 0x400, GLYPH_LO = 0xab3, GLYPH_HI = 0xbd1;

// IN0, one byte: DVG halted (decoded instantly), service, tilt, diag step
// and unused bits all idle; bit 6 the 3 kHz clock
static const uint8_t IN0_IDLE = 0xbf, IN0_CLOCK_3K = 0x40;

// IN1 bits; the read returns 0x80 when the bit is set
enum : uint8_t {
  IN1_START1 = 0, // active high
  IN1_COIN1 = 1,  // active low
  IN1_COIN3 = 3,  // active low
  IN1_SELECT = 4,
  IN1_ABORT = 5,
  IN1_RIGHT = 6,
  IN1_LEFT = 7,
};

// zero page game state (ROM 0x627a, MAME RAM dumps): 0 attract, 0x20
// credit / mission select, 0x40 flying, 0x80 landed or crashed, back to 0
// at game over
static const uint16_t RAM_GAME_STATE = 0x22;
static const uint8_t GAME_RUNNING = 0xc0;

// thrust lever: full range in 42 frames (0.7 s); MAME caps the pedal at 254
static const uint8_t THRUST_MAX = 254, THRUST_STEP = 6;

// outlatch mission lamps, active high
enum : uint8_t { LAMP_COMMAND = 0x01, LAMP_PRIME = 0x02, LAMP_CADET = 0x04, LAMP_TRAINING = 0x08 };

// vector ROM glyph subroutines
enum : uint16_t {
  CHAR_A = 0xadf, CHAR_C = 0xaf4, CHAR_D = 0xafa, CHAR_E = 0xb02, CHAR_G = 0xb11, CHAR_I = 0xb21,
  CHAR_M = 0xb39, CHAR_N = 0xb3f, CHAR_O = 0xb44, CHAR_P = 0xb4a, CHAR_R = 0xb5a, CHAR_T = 0xb69,
};
static const uint8_t MISSION_LEN = 8; // glyphs in the longest name
static const uint16_t MISSION_NAMES[4][MISSION_LEN] = {
  {CHAR_T, CHAR_R, CHAR_A, CHAR_I, CHAR_N, CHAR_I, CHAR_N, CHAR_G},
  {CHAR_C, CHAR_A, CHAR_D, CHAR_E, CHAR_T},
  {CHAR_P, CHAR_R, CHAR_I, CHAR_M, CHAR_E},
  {CHAR_C, CHAR_O, CHAR_M, CHAR_M, CHAR_A, CHAR_N, CHAR_D},
};
// right of SCORE in the left HUD block (llander_layout moves it to the
// top border)
static const uint16_t MISSION_X = 300, MISSION_Y = 748;
enum : uint16_t { DVG_LABS = 0xa000, DVG_HALT = 0xb000, DVG_JSRL = 0xc000 };

llander::llander()
  : dvgMachine(main_read, main_write),
    llander_rom(llander_rom_blob),
    llander_vrom(llander_vrom_blob) {
  static_assert(RAM_MASK < VRAM_OFS, "Lunar Lander RAM overlaps VRAM");
  static_assert(sizeof(mission) >= 2 * (2 + MISSION_LEN + 1), "mission list too small");
  rom = llander_rom.data();
  vrom = llander_vrom.data();
  dvg_mem = dvg::Memory{nullptr, vrom, DVG_ROM_BASE, VROM_SIZE / 2, GLYPH_LO, GLYPH_HI};
}

void llander::reset() {
  dvgMachine::reset();
  thrust = 0;
  lamps = 0;
}

uint8_t llander::in1(uint8_t bit) const {
  const unsigned int k = input->buttons_get();
  switch (bit) {
    case IN1_START1: return input_bit(k & BUTTON_START);
    case IN1_COIN1: return input_bit(!(k & BUTTON_COIN));
    case IN1_COIN3: return input_bit(true);
    case IN1_SELECT: return input_bit(!in_game() && (k & BUTTON_FIRE));
    case IN1_ABORT: return input_bit(in_game() && (k & BUTTON_FIRE));
    case IN1_RIGHT: return input_bit(k & BUTTON_RIGHT);
    case IN1_LEFT: return input_bit(k & BUTTON_LEFT);
  }
  return input_bit(false); // COIN2
}

// FIRE is Select Game before START, Abort in game: Select in game would
// switch the mission
bool llander::in_game() const {
  return memory[RAM_GAME_STATE] & GAME_RUNNING;
}

// Lever moves while UP/DOWN is held; not while START/EXTRA is held,
// where UP/DOWN set the volume.
void llander::frame_input() {
  const unsigned int k = input->buttons_get();
  if (k & BUTTON_EXTRA) {
    return;
  }
  if (k & BUTTON_UP) {
    thrust = thrust > THRUST_MAX - THRUST_STEP ? THRUST_MAX : thrust + THRUST_STEP;
  }
  if (k & BUTTON_DOWN) {
    thrust = thrust < THRUST_STEP ? 0 : thrust - THRUST_STEP;
  }
}

uint8_t llander::main_read(m6502_t *cpu, uint16_t a) {
  llander *s = static_cast<llander *>(cpu->user);
  a &= ADDR_MASK;
  if (a >= ROM) {
    return s->rom[a - ROM];
  }
  if (a < RAM_MIRROR_END) {
    return s->memory[a & RAM_MASK];
  }
  if (a >= VRAM && a < VRAM + VRAM_SIZE) {
    return s->memory[VRAM_OFS + a - VRAM];
  }
  if (a >= VROM && a < VROM + VROM_SIZE) {
    return s->vrom[a - VROM];
  }
  if (a == IN0) {
    return IN0_IDLE | (s->clock_3k() ? IN0_CLOCK_3K : 0);
  }
  if (a == THRUST) {
    return s->thrust;
  }

  switch (a & ~7) {
    case IN1: return s->in1(a & 7);
    case DSW: return dsw_read(LLANDER_DSW, a);
  }
  return 0;
}

void llander::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  llander *s = static_cast<llander *>(cpu->user);
  a &= ADDR_MASK;
  if (a < RAM_MIRROR_END) {
    s->memory[a & RAM_MASK] = v;
    return;
  }
  if (a >= VRAM && a < VRAM + VRAM_SIZE) {
    s->memory[VRAM_OFS + a - VRAM] = v;
    return;
  }

  switch (a) {
    case DVG_GO: s->go(); break;
    case OUTLATCH: s->lamps = v; break;
    case SOUND: s->soundregs[LL_SND_CTRL] = v; break;
    case NOISE_RESET: s->soundregs[LL_SND_NOISE_RESET]++; break;
  }
  // 0x3400 watchdog, 0x5800 VG INC: ignored
}

// The cabinet shows the selected mission with lamps only; draw its name
// with the game's own glyphs instead. nullptr when no lamp is lit.
const uint8_t *llander::mission_list() {
  static const uint8_t LAMPS[4] = {LAMP_TRAINING, LAMP_CADET, LAMP_PRIME, LAMP_COMMAND};
  const uint8_t lit = lamps; // written by the emulation core
  uint8_t m = 0;
  while (m < 4 && !(lit & LAMPS[m])) {
    m++;
  }
  if (m == 4) {
    return nullptr;
  }

  uint16_t words[2 + MISSION_LEN + 1];
  uint8_t n = 0;
  words[n++] = DVG_LABS | MISSION_Y;
  words[n++] = MISSION_X; // global scale 0
  for (uint8_t i = 0; i < MISSION_LEN && MISSION_NAMES[m][i]; i++) {
    words[n++] = DVG_JSRL | MISSION_NAMES[m][i];
  }
  words[n++] = DVG_HALT;

  for (uint8_t i = 0; i < n; i++) {
    mission[2 * i] = words[i] & 0xff;
    mission[2 * i + 1] = words[i] >> 8;
  }
  return mission;
}

uint16_t llander::decode_frame(const uint8_t *vram) {
  const uint16_t n = dvgMachine::decode_frame(vram);
  const uint8_t *list = mission_list();
  if (!list || n == MAX_LINES) {
    return n;
  }
  dvg::Memory m = dvg_mem;
  m.ram = list;
  return n + dvg::decode(m, lines + n, MAX_LINES - n);
}

uint16_t llander::map_lines(uint16_t count) {
  return llander_layout::map(lines, count);
}

#ifdef LED_PIN
// logo colors
static const CRGB LLANDER_LED_ORANGE(0xffaa28), LLANDER_LED_YELLOW(0xffdc3c),
    LLANDER_LED_WHITE(0xffffff);

void llander::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      LLANDER_LED_ORANGE, LLANDER_LED_YELLOW, LLANDER_LED_WHITE, LLANDER_LED_WHITE,
      LLANDER_LED_WHITE, LLANDER_LED_YELLOW, LLANDER_LED_ORANGE};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// Engine flame: LEDs glow orange with the thrust sound volume (0-7).
void llander::gameLeds(CRGB *leds) {
  menuLeds(leds);
  const uint8_t volume = soundregs[LL_SND_CTRL] & LL_THRUST_MASK;
  if (!volume) {
    return;
  }
  fill_solid(leds, NUM_LEDS, CRGB(LLANDER_LED_ORANGE).nscale8(volume * 36));
}
#endif
