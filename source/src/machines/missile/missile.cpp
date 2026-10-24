#include "missile.h"
#include "missile_rom.h"
#include "../../emulation/pokey.h"

using namespace missile_video;

// memory map (MAME missile_map), CPU address & 0x7fff
enum : uint16_t {
  ADDR_MASK = 0x7fff,
  VRAM_END = 0x4000,
  POKEY_END = 0x4800,    // 0x4000-0x47ff, 16 registers mirrored
  IO_MASK = 0xff00,      // 0x4800-0x4dff, mirror 0xff
  IO_IN0 = 0x4800,       // R: IN0 / trackball, W: outputs
  IO_IN1 = 0x4900,
  IO_R10 = 0x4a00,
  IO_PALETTE = 0x4b00,
  IO_IRQ_ACK = 0x4d00,
  ROM_BASE = 0x5000,
  WPROM_OFFSET = 0x3000, // write PROM in rom[]
};

// POKEY register offsets beyond AUDF1..AUDC4 (0-7)
enum : uint8_t { POKEY_AUDCTL = 0x08, POKEY_ALLPOT = 0x08, POKEY_RANDOM = 0x0a, POKEY_SKCTL = 0x0f };
static const uint8_t AUDCTL_POLY9 = 0x80;
static const uint8_t SK_RESET = 0x03; // SKCTL bits, both clear = reset

// IN0, active low
enum : uint8_t { IN0_START1 = 0x10, IN0_COIN1 = 0x20 };
// IN1
enum : uint8_t {
  IN1_FIRE_RIGHT = 0x01, // fire buttons active low
  IN1_FIRE_CENTER = 0x02,
  IN1_FIRE_LEFT = 0x04,
  IN1_DIR = 0x18,        // trackball direction lines, 0
  IN1_VBLANK = 0x80,     // active high
};
static const uint8_t OUT_CTRLD = 0x01;
static const uint8_t MADSEL_OPCODE_MASK = 0x1f, MADSEL_OPCODE = 0x01; // (zp,X)
// stick -> trackball counts per frame (1 count = 1 pixel): 1, +1 every 8 frames
// held, max 6. Must stay < 8, the game reads a 4-bit signed delta.
static const uint8_t TRACK_RAMP = 8, TRACK_MAX = 6;
static const uint8_t TRACK_MASK = 0x0f;
// FIRE picks the base by cursor pixel x: thirds of the screen. An empty base
// (ammo 0, RAM 0xa0-0xa2 found with MAME) hands over to the nearest base with
// ammo, by distance to the base's x.
static const uint8_t ZONE_LEFT_END = 86, ZONE_CENTER_END = 171;
static const uint8_t BASE_X[3] = {21, 128, 235};

missile::missile() : missile_rom(missile_rom_blob) {
  memset(&m_cpu, 0, sizeof(m_cpu));
  m_cpu.read = main_read;
  m_cpu.write = main_write;
  m_cpu.fetch = main_fetch;
  m_cpu.user = this;
  rom = missile_rom.data();
  wprom = rom + WPROM_OFFSET;

  // pixel p of a VRAM byte uses bits p (pen 2) and p + 4 (pen 4)
  for (int b = 0; b < 256; b++) {
    uint32_t q = 0;
    for (int p = 0; p < 4; p++) {
      const uint8_t s = (uint8_t)(b >> p);
      q |= (uint32_t)(((s >> 2) & 4) | ((s << 1) & 2)) << (8 * p);
    }
    quad[b] = q;
  }
}

void missile::reset() {
  machineBase::reset();
  memset(pal, 0, sizeof(pal));
  ctrld = madsel = 0;
  track_x = track_y = track_held = 0;
  frame_cycles = total_cycles = 0;
  random_base = 0;
  allpot = 0;
  in_vblank = false;
  m6502_reset(&m_cpu);
}

// CTRLD selects trackball counters or switches
uint8_t missile::in0() const {
  if (ctrld) {
    return (uint8_t)((track_y << 4) | track_x);
  }

  const unsigned int k = input->buttons_get();
  uint8_t v = 0xff;
  if (k & BUTTON_COIN) {
    v &= ~IN0_COIN1;
  }
  if (k & BUTTON_START) {
    v &= ~IN0_START1;
  }
  return v;
}

// FIRE: base whose third holds the cursor, else the nearest with ammo. Controller L1/A/R1: left/center/right.
uint8_t missile::in1() const {
  const unsigned int k = input->buttons_get();
  uint8_t v = (uint8_t)~(IN1_DIR | IN1_VBLANK);
  if (in_vblank) {
    v |= IN1_VBLANK;
  }

  if (k & BUTTON_FIRE) {
    static const uint8_t FIRE_BASE[3] = {IN1_FIRE_LEFT, IN1_FIRE_CENTER, IN1_FIRE_RIGHT};
    const uint8_t x = memory[CURSOR_X];
    uint8_t base = x < ZONE_LEFT_END ? 0 : x < ZONE_CENTER_END ? 1 : 2;
    if (memory[AMMO + base] == 0) {
      uint8_t best = 255;
      for (uint8_t i = 0; i < 3; i++) {
        const uint8_t d = x > BASE_X[i] ? x - BASE_X[i] : BASE_X[i] - x;
        if (memory[AMMO + i] != 0 && d < best) {
          best = d;
          base = i;
        }
      }
    }
    v &= ~FIRE_BASE[base];
  }
  if (k & BUTTON_L1) {
    v &= ~IN1_FIRE_LEFT;
  }
  if (k & BUTTON_A) {
    v &= ~IN1_FIRE_CENTER;
  }
  if (k & BUTTON_R1) {
    v &= ~IN1_FIRE_RIGHT;
  }
  return v;
}

// once per frame: held direction moves the counters, accelerating
void missile::update_trackball() {
  const unsigned int k = input->buttons_get();
  if (!(k & (BUTTON_LEFT | BUTTON_RIGHT | BUTTON_UP | BUTTON_DOWN))) {
    track_held = 0;
    return;
  }

  if (track_held < TRACK_RAMP * TRACK_MAX) {
    track_held++;
  }
  uint8_t step = (uint8_t)(1 + track_held / TRACK_RAMP);
  if (step > TRACK_MAX) {
    step = TRACK_MAX;
  }

  // counters up = cursor right / up (checked in MAME)
  if (k & BUTTON_RIGHT) {
    track_x += step;
  }
  if (k & BUTTON_LEFT) {
    track_x -= step;
  }
  if (k & BUTTON_UP) {
    track_y += step;
  }
  if (k & BUTTON_DOWN) {
    track_y -= step;
  }
  track_x &= TRACK_MASK;
  track_y &= TRACK_MASK;
}

bool missile::pokey_running() const {
  return soundregs[Pokey::REG_SKCTL] & SK_RESET;
}

// ALLPOT reads the R8 option switches
uint8_t missile::pokey_read(uint8_t reg) {
  if (reg == POKEY_ALLPOT) {
    if (pokey_running()) {
      allpot = MISSILE_R8;
    }
    return allpot;
  }
  if (reg == POKEY_RANDOM) {
    const bool poly9 = soundregs[Pokey::REG_AUDCTL] & AUDCTL_POLY9;
    return Pokey::random(pokey_running() ? cpu_clock() - random_base : 0, poly9);
  }
  return 0;
}

void missile::pokey_write(uint8_t reg, uint8_t v) {
  if (reg <= POKEY_AUDCTL) {
    soundregs[reg] = v;
  } else if (reg == POKEY_SKCTL) {
    if (!pokey_running() && (v & SK_RESET)) {
      random_base = cpu_clock();
    }
    soundregs[Pokey::REG_SKCTL] = v;
  }
}

// true on the data access of an armed (zp,X) instruction
bool missile::madsel_access() {
  return madsel && --madsel == 0;
}

uint8_t missile::read(uint16_t a) {
  if (madsel_access()) {
    if (madExtraCycle(a)) {
      frame_cycles++;
    }
    return madRead(memory, a);
  }

  a &= ADDR_MASK;
  if (a >= ROM_BASE) {
    return rom[a - ROM_BASE];
  }
  if (a < VRAM_END) {
    return memory[a];
  }
  if (a < POKEY_END) {
    return pokey_read(a & 0x0f);
  }

  switch (a & IO_MASK) {
    case IO_IN0: return in0();
    case IO_IN1: return in1();
    case IO_R10: return MISSILE_R10;
  }
  return 0xff; // unmap_value_high
}

void missile::write(uint16_t a, uint8_t v) {
  if (madsel_access()) {
    madWrite(memory, wprom, a, v);
    if (madExtraCycle(a)) {
      frame_cycles++;
    }
    return;
  }

  a &= ADDR_MASK;
  if (a < VRAM_END) {
    memory[a] = v;
    return;
  }
  if (a < POKEY_END) {
    pokey_write(a & 0x0f, v);
    return;
  }

  switch (a & IO_MASK) {
    case IO_IN0: ctrld = v & OUT_CTRLD; return; // flip, LEDs, coin counters ignored
    case IO_PALETTE: pal[a & 7] = paletteColor(v); return;
    case IO_IRQ_ACK: m_cpu.irq = 0; return;
  }
}

// MAME load_madsel: (zp,X) opcode fetched while the IRQ line is low
uint8_t missile::main_fetch(m6502_t *cpu, uint16_t a) {
  missile *m = static_cast<missile *>(cpu->user);
  const uint8_t op = m->read(a);
  if ((op & MADSEL_OPCODE_MASK) == MADSEL_OPCODE && !cpu->irq) {
    m->madsel = MADSEL_ACCESSES;
  }
  return op;
}

uint8_t missile::main_read(m6502_t *cpu, uint16_t a) {
  return static_cast<missile *>(cpu->user)->read(a);
}

void missile::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  static_cast<missile *>(cpu->user)->write(a, v);
}

uint32_t missile::cpu_clock() const {
  return total_cycles + frame_cycles;
}

void missile::run_until(uint32_t cycle) {
  while (frame_cycles < cycle) {
    frame_cycles += m6502_step(&m_cpu);
  }
}

// frame starts at V = 0. IRQ level per MAME clock_irq; palette snapshot at
// vblank end.
void missile::run_frame() {
  update_trackball();

  uint32_t end = 0;
  for (int line = 0; line < LINES; line += LINE_STEP) {
    if (line == FIRST_LINE) {
      vblank.publish([this](VideoState &v) { memcpy(v.pal, pal, sizeof(v.pal)); });
    }
    in_vblank = line < FIRST_LINE;
    if (line % IRQ_LINES == 0) {
      m_cpu.irq = 1;
    } else if (line % IRQ_LINES == IRQ_LENGTH) {
      m_cpu.irq = 0;
    }

    end += LINE_STEP * (line < SLOW_LINE ? CYCLES_FAST : CYCLES_SLOW);
    run_until(end);
  }
  frame_cycles -= CYCLES_PER_FRAME; // keep the overshoot
  total_cycles += CYCLES_PER_FRAME;
  if (!game_started) {
    game_started = 1;
  }
}

void missile::prepare_frame() {
  vblank.read(video);
}

void missile::render_row(short row) {
  // frame_buffer is already black outside the game area
  const int strip = row - FIRST_STRIP;
  if (strip < 0 || strip >= VIS_LINES / 8) {
    return;
  }

  const uint16_t *p = video.pal;
  for (int l = 0; l < 8; l++) {
    const int y = FIRST_LINE + strip * 8 + l;
    uint16_t *dst = frame_buffer + l * OUT_WIDTH;
    if (y == FIRST_LINE) { // MAME VBEND 25: line 24 is garbage
      memset(dst, 0, OUT_WIDTH * sizeof(uint16_t));
      continue;
    }

    const uint8_t *src = memory + y * LINE_BYTES + CROP_LEFT / 4;
    if (y < BIT3_LINE) {
      for (int i = 0; i < OUT_WIDTH / 4; i++) {
        const uint32_t q = quad[src[i]];
        *dst++ = p[q & 0xff];
        *dst++ = p[(q >> 8) & 0xff];
        *dst++ = p[(q >> 16) & 0xff];
        *dst++ = p[q >> 24];
      }
      continue;
    }

    // lines >= 224: 3rd bit, one byte per 8 pixels at even offsets
    const uint8_t *src3 = memory + bit3Addr((uint16_t)(y << 8)) + (CROP_LEFT / 8) * 2;
    for (int i = 0; i < OUT_WIDTH / 8; i++) {
      const uint8_t b3 = src3[i * 2];
      for (int h = 0; h < 2; h++) {
        const uint32_t q = quad[src[i * 2 + h]];
        for (int px = 0; px < 4; px++) {
          *dst++ = p[((q >> (8 * px)) & 0xff) | ((b3 >> (h * 4 + px)) & 1)];
        }
      }
    }
  }
}

#ifdef LED_PIN
// logo colors: red, blue, yellow
static const CRGB MISSILE_LED_A(0xd91e1e), MISSILE_LED_B(0x2655e0), MISSILE_LED_C(0xe8c800);
static const uint8_t FIRE_BLINK = 4; // frames per blink phase

void missile::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      MISSILE_LED_A, MISSILE_LED_B, MISSILE_LED_C, MISSILE_LED_C,
      MISSILE_LED_C, MISSILE_LED_B, MISSILE_LED_A};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// red alarm blink while FIRE is held, else the menu colors
void missile::gameLeds(CRGB *leds) {
  if (!(input->buttons_get() & BUTTON_FIRE)) {
    menuLeds(leds);
    return;
  }

  led_frame++;
  fill_solid(leds, NUM_LEDS, (led_frame & FIRE_BLINK) ? MISSILE_LED_A : CRGB::Black);
}
#endif
