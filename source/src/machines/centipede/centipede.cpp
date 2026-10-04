#include "centipede.h"
#include "centipede_rom.h"
#include "centipede_gfx.h"
#include "../../emulation/pokey.h"
#include "../../emulation/arena.h"

static const uint16_t PLANE = 0x800; // MSB plane offset in gfx

// memory map (MAME centiped_base_map + centiped_map), 14 bit address bus
enum : uint16_t {
  ADDR_MASK = 0x3fff,
  RAM_END = 0x0800, // work RAM, playfield, sprites
  DSW1 = 0x0800,
  DSW2 = 0x0801,
  IN0 = 0x0c00,
  IN1 = 0x0c01,
  IN2 = 0x0c02,
  IN3 = 0x0c03,
  POKEY_BASE = 0x1000,
  PALETTE_BASE = 0x1400,
  EAROM_WRITE = 0x1600, // 0x1600-0x163f: address + data
  EAROM_CONTROL = 0x1680,
  EAROM_READ = 0x1700, // 0x1700-0x173f
  EAROM_SIZE = 0x40,
  IRQ_ACK = 0x1800,
  OUTLATCH = 0x1c00,  // 0x1c00-0x1c07, data bit 7
  ROM_BASE = 0x2000,
  REG_BLOCK_MASK = 0xfff0, // POKEY and palette decode 16 registers
};

// POKEY register offsets beyond AUDF1..AUDC4 (0-7)
enum : uint8_t { POKEY_AUDCTL = 0x08, POKEY_RANDOM = 0x0a, POKEY_SKCTL = 0x0f };
static const uint8_t AUDCTL_POLY9 = 0x80;
static const uint8_t SK_RESET = 0x03; // SKCTL bits, both clear = reset

// outlatch bits, MAME centiped_base: 0-2 coin counters, 3-4 START lamps
// (active low), 7 flip
enum : uint8_t { OUTLATCH_COIN = 0, OUTLATCH_LAMP = 3 };
static const uint8_t COIN_LED_FRAMES = 30; // counter pulse is too short to see
static const uint8_t OUTLATCH_DATA = 0x80;

// active low input bits
enum : uint8_t {
  IN1_START1 = 0x01,
  IN1_FIRE1 = 0x04,
  IN1_COIN1 = 0x20,
  IN3_UP = 0x10,
  IN3_DOWN = 0x20,
  IN3_LEFT = 0x40,
  IN3_RIGHT = 0x80,
};
// IN0 active high bits
enum : uint8_t { IN0_SERVICE_OFF = 0x20, IN0_VBLANK = 0x40 };

// raster timing: IRQ every 32 lines, vblank from line 240, 16 px letterbox
static const int IRQ_STEP = 16;
static const int VBLANK_LINE = 240;
static const int SPRITE_CLIP_X = 248;
static const int TOP_BORDER = 16;

static inline uint8_t gfx_pen(const unsigned char *gfx, uint16_t row, int x) {
  return (((gfx[PLANE + row] << x) & 0x80) >> 6) | (((gfx[row] << x) & 0x80) >> 7);
}

// MAME centiped_paletteram_w: inverted bits, bit 3 low dims blue, else green
static void centipede_rgb(uint8_t d, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = (d & 1) ? 0 : 0xff;
  g = (d & 2) ? 0 : 0xff;
  b = (d & 4) ? 0 : 0xff;
  if (!(d & 8)) {
    if (b) {
      b = 0xc0;
    } else if (g) {
      g = 0xc0;
    }
  }
}

static uint16_t centipede_color(uint8_t d) {
  uint8_t r, g, b;
  centipede_rgb(d, r, g, b);
  uint16_t rgb = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (rgb >> 8) | (rgb << 8);
}

centipede::centipede()
  : centipede_gfx(centipede_gfx_blob),
    centipede_rom(centipede_rom_blob) {
  memset(&m_cpu, 0, sizeof(m_cpu));
  m_cpu.read = main_read;
  m_cpu.write = main_write;
  m_cpu.user = this;
  rom = centipede_rom.data();
  gfx = centipede_gfx.data();
}

void centipede::start() {
  if (tile_cols) {
    return; // start() runs twice
  }

  tile_cols = Arena::alloc<uint16_t>(64 * 2 * 8);
  sprite_cols = Arena::alloc<uint32_t>(128 * 2 * 8);

  // tiles use gfx codes 0x40..0x7f (MAME centiped_get_tile_info)
  for (int code = 0; code < 64; code++) {
    for (int fy = 0; fy < 2; fy++) {
      for (int cx = 0; cx < 8; cx++) {
        uint16_t w = 0;
        for (int i = 0; i < 8; i++) {
          w |= gfx_pen(gfx, (0x40 + code) * 8 + (fy ? 7 - i : i), cx) << (2 * i);
        }
        tile_cols[(code * 2 + fy) * 8 + cx] = w;
      }
    }
  }

  for (int code = 0; code < 128; code++) {
    for (int fy = 0; fy < 2; fy++) {
      for (int cx = 0; cx < 8; cx++) {
        uint32_t w = 0;
        for (int i = 0; i < 16; i++) {
          w |= (uint32_t)gfx_pen(gfx, code * 16 + (fy ? 15 - i : i), cx) << (2 * i);
        }
        sprite_cols[(code * 2 + fy) * 8 + cx] = w;
      }
    }
  }
}

void centipede::reset() {
  machineBase::reset();
  frame_cycles = total_cycles = random_base = 0;
  in_vblank = false;
  memset(palette_ram, 0, sizeof(palette_ram));
  start_lamp = false;
  coin_leds = 0;
  earom.reset();
  m6502_reset(&m_cpu);
}

uint8_t centipede::in1() const {
  const uint8_t k = input->buttons_get();
  uint8_t v = 0xff;
  if (k & BUTTON_START) {
    v &= ~IN1_START1;
  }
  if (k & BUTTON_FIRE) {
    v &= ~IN1_FIRE1;
  }
  if (k & BUTTON_COIN) {
    v &= ~IN1_COIN1;
  }
  return v;
}

uint8_t centipede::in3() const {
  const uint8_t k = input->buttons_get();
  uint8_t v = 0xff;
  if (k & BUTTON_UP) {
    v &= ~IN3_UP;
  }
  if (k & BUTTON_DOWN) {
    v &= ~IN3_DOWN;
  }
  if (k & BUTTON_LEFT) {
    v &= ~IN3_LEFT;
  }
  if (k & BUTTON_RIGHT) {
    v &= ~IN3_RIGHT;
  }
  return v;
}

uint8_t centipede::main_read(m6502_t *cpu, uint16_t a) {
  centipede *s = static_cast<centipede *>(cpu->user);
  a &= ADDR_MASK;
  if (a >= ROM_BASE) {
    return s->rom[a - ROM_BASE];
  }
  if (a < RAM_END) {
    return s->memory[a];
  }

  switch (a) {
    case DSW1: return CENTIPEDE_DSW1;
    case DSW2: return CENTIPEDE_DSW2;
    case IN0: return IN0_SERVICE_OFF | (s->in_vblank ? IN0_VBLANK : 0); // trackball idle, upright
    case IN1: return s->in1();
    case IN2: return 0x00; // trackball idle
    case IN3: return s->in3();
  }

  // RANDOM is the only POKEY read the game needs; pots etc. read 0
  if ((a & REG_BLOCK_MASK) == POKEY_BASE) {
    if ((a & 0x0f) != POKEY_RANDOM) {
      return 0;
    }
    // polys hold at 0 in reset and restart on release; the game checks
    // that two reads during reset match, else it inflates the credits and
    // traps in the IRQ handler (0x38ac)
    const bool poly9 = s->soundregs[Pokey::REG_AUDCTL] & AUDCTL_POLY9;
    const uint32_t clock = s->pokey_running() ? s->cpu_clock() - s->random_base : 0;
    return Pokey::random(clock, poly9);
  }

  if (a >= EAROM_READ && a < EAROM_READ + EAROM_SIZE) {
    return s->earom.read();
  }
  return 0;
}

void centipede::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  centipede *s = static_cast<centipede *>(cpu->user);
  a &= ADDR_MASK;
  if (a < RAM_END) {
    s->memory[a] = v;
    return;
  }

  // POKEY: AUDF1..AUDC4, AUDCTL, SKCTL go to soundregs (layout in pokey.h)
  if ((a & REG_BLOCK_MASK) == POKEY_BASE) {
    const uint8_t r = a & 0x0f;
    if (r <= POKEY_AUDCTL) {
      s->soundregs[r] = v;
    } else if (r == POKEY_SKCTL) {
      if (!s->pokey_running() && (v & SK_RESET)) {
        s->random_base = s->cpu_clock();
      }
      s->soundregs[Pokey::REG_SKCTL] = v;
    }
    return;
  }

  if ((a & REG_BLOCK_MASK) == PALETTE_BASE) {
    s->palette_ram[a & 0x0f] = v;
    return;
  }
  if (a >= EAROM_WRITE && a < EAROM_WRITE + EAROM_SIZE) {
    s->earom.latch(a, v);
    return;
  }
  if (a == EAROM_CONTROL) {
    s->earom.control(v);
    return;
  }
  if (a == IRQ_ACK) {
    cpu->irq = 0;
    return;
  }
  // outlatch: only START1 lamp and coin counters, for the LEDs; flip ignored
  if (a == OUTLATCH + OUTLATCH_LAMP) {
    s->start_lamp = !(v & OUTLATCH_DATA);
  } else if (a >= OUTLATCH + OUTLATCH_COIN && a < OUTLATCH + OUTLATCH_LAMP && (v & OUTLATCH_DATA)) {
    s->coin_leds = COIN_LED_FRAMES;
  }
  // 0x2000 watchdog: ignored
}

bool centipede::pokey_running() const {
  return soundregs[Pokey::REG_SKCTL] & SK_RESET;
}

uint32_t centipede::cpu_clock() const {
  return total_cycles + frame_cycles;
}

// cycle: target cycle count within the frame
void centipede::run_until(uint32_t cycle) {
  while (frame_cycles < cycle) {
    frame_cycles += m6502_step(&m_cpu);
  }
}

void centipede::run_frame() {
  for (int line = 0; line < LINES; line += IRQ_STEP) {
    // MAME generate_interrupt: IRQ clocked on the rising edge of 16V,
    // asserted at lines 48, 112, 176, 240, 256
    if (line & 16) {
      m_cpu.irq = ((line - 1) & 32) ? 1 : 0;
    }

    in_vblank = line >= VBLANK_LINE;
    if (line == VBLANK_LINE) {
      vblank.publish([this](VideoState &v) {
        memcpy(v.ram, memory + VIDEO_RAM, sizeof(v.ram));
        memcpy(v.palette, palette_ram, sizeof(v.palette));
      });
    }

    const int end = line + IRQ_STEP < LINES ? line + IRQ_STEP : LINES;
    run_until((uint32_t)end * CYCLES_PER_FRAME / LINES);
  }
  frame_cycles -= CYCLES_PER_FRAME; // keep the overshoot
  total_cycles += CYCLES_PER_FRAME;
  if (!game_started) {
    game_started = 1;
  }
}

void centipede::prepare_frame() {
  vblank.read(video);

  // only palette offsets with bit 2 set exist: 4-7 tiles, 12-15 sprites
  uint16_t sprite_pal[4];
  for (int i = 0; i < 4; i++) {
    tile_pal[i] = centipede_color(video.palette[4 + i]);
    sprite_pal[i] = centipede_color(video.palette[12 + i]);
  }

  const uint8_t *sr = video.ram + SPRITE_OFS;
  for (int i = 0; i < 16; i++) {
    const uint8_t b = sr[i], color = sr[0x30 + i];
    Sprite &s = spr[i];
    const int code = ((b & 0x3e) >> 1) | ((b & 1) << 6);
    s.base = (code * 2 + (b >> 7)) * 8;
    s.flipx = (b >> 6) & 1;
    s.x = sr[0x20 + i];
    s.y = 240 - sr[0x10 + i];
    s.opaque = 0;
    for (int p = 1; p < 4; p++) {
      const uint8_t sel = (color >> (2 * (p - 1))) & 3;
      s.color[p] = sprite_pal[sel];
      if (sel) {
        s.opaque |= 1 << p; // MAME init_penmask
      }
    }
  }
}

// ROT270: output line py shows native column x = 255 - py, output
// column px = native y. Orientation needs one hardware check.
void centipede::render_line(uint16_t *line, int x) const {
  const int tx = x >> 3, cx = x & 7;
  for (int ty = 0; ty < 30; ty++) {
    const uint8_t d = video.ram[ty * 32 + tx];
    uint16_t w = tile_cols[((d & 0x3f) * 2 + (d >> 7)) * 8 + ((d & 0x40) ? 7 - cx : cx)];
    uint16_t *p = line + ty * 8;
    for (int i = 0; i < 8; i++, w >>= 2) {
      p[i] = tile_pal[w & 3];
    }
  }

  if (x >= SPRITE_CLIP_X) {
    return;
  }

  // sprites 0..15, later ones on top
  for (int i = 0; i < 16; i++) {
    const Sprite &s = spr[i];
    const int c = x - s.x;
    if ((unsigned)c >= 8) {
      continue;
    }
    uint32_t w = sprite_cols[s.base + (s.flipx ? 7 - c : c)];
    for (int k = 0, y = s.y; k < 16; k++, y++, w >>= 2) {
      const uint8_t pen = w & 3;
      if (((s.opaque >> pen) & 1) && (unsigned)y < VBLANK_LINE) {
        line[y] = s.color[pen];
      }
    }
  }
}

void centipede::render_row(short strip) {
  for (int oy = 0; oy < 8; oy++) {
    const int py = strip * 8 + oy - TOP_BORDER; // 256 rows centered in 288
    if (py < 0 || py >= 256) {
      continue;
    }
    render_line(frame_buffer + oy * 240, 255 - py);
  }
}

#ifdef LED_PIN
// logo colors
static const CRGB CENTIPEDE_LED_GREEN(0x50b147), CENTIPEDE_LED_LIGHTGREEN(0x90c734),
    CENTIPEDE_LED_YELLOW(0xfcd736), CENTIPEDE_LED_BRIGHTYELLOW(0xfdfa59);

void centipede::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      CENTIPEDE_LED_GREEN, CENTIPEDE_LED_LIGHTGREEN, CENTIPEDE_LED_YELLOW, CENTIPEDE_LED_BRIGHTYELLOW,
      CENTIPEDE_LED_YELLOW, CENTIPEDE_LED_LIGHTGREEN, CENTIPEDE_LED_GREEN};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// sprite codes, traced from sprite RAM in attract and play
static const uint8_t SPR_SEGMENT_END = 0x08; // centipede head/body 0x00-0x07
static const uint8_t SPR_PLAYER = 0x08;
static const uint8_t SPR_DEATH = 0x10; // code & 0x3c: 0x10-0x13, 0x50-0x53
static const uint8_t SPR_SPIDER_FIRST = 0x0a, SPR_SPIDER_LAST = 0x0d; // code & 0x3f
static const int SPIDER_RANGE = 64; // native px, player to spider

// In priority order: coin inserted: white. Player death: red pulse. POKEY
// noise (explosions): bright flash. START1 lamp off (attract, no credit):
// logo colors, so with a credit they blink along with the lamp. Else a two
// LED centipede in the on screen centipede's color bouncing across the
// strip, turning red from the edges as the spider closes in on the player.
void centipede::gameLeds(CRGB *leds) {
  if (coin_leds) {
    coin_leds--;
    fill_solid(leds, NUM_LEDS, CRGB::White);
    return;
  }

  const uint8_t *sr = video.ram + SPRITE_OFS;
  int player = -1, spider = -1;
  for (int i = 0; i < 16; i++) {
    const uint8_t code = ((sr[i] & 0x3e) >> 1) | ((sr[i] & 1) << 6);
    if ((code & 0x3c) == SPR_DEATH) {
      fill_solid(leds, NUM_LEDS, (code & 0x40) ? CRGB(0xff, 0, 0) : CRGB(0x40, 0, 0));
      return;
    }
    if (code < SPR_SEGMENT_END) {
      // body color: pen 3, palette select in color byte bits 5-4
      const uint8_t sel = (sr[0x30 + i] >> 4) & 3;
      if (sel) {
        worm_pal = 12 + sel;
      }
    } else if (code == SPR_PLAYER) {
      player = i;
    } else if ((code & 0x3f) >= SPR_SPIDER_FIRST && (code & 0x3f) <= SPR_SPIDER_LAST) {
      spider = i;
    }
  }

  bool noise = false;
  for (int c = 0; c < 4; c++) {
    const uint8_t audc = soundregs[2 * c + 1];
    noise |= !(audc & 0x20) && (audc & 0x0f);
  }
  if (noise) {
    fill_solid(leds, NUM_LEDS, CENTIPEDE_LED_BRIGHTYELLOW);
    return;
  }

  if (!start_lamp) {
    menuLeds(leds);
    return;
  }

  // one step per 4 frames, ping-pong over the strip, body one LED behind
  CRGB color;
  centipede_rgb(video.palette[worm_pal], color.r, color.g, color.b);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  led_frame = (led_frame + 1) % (4 * 2 * (NUM_LEDS - 1));
  const int t = led_frame / 4;
  const int dir = t < NUM_LEDS - 1 ? 1 : -1;
  const int head = dir > 0 ? t : 2 * (NUM_LEDS - 1) - t;
  const int body = head - dir;
  leds[head] = color;
  if (body >= 0 && body < NUM_LEDS) {
    leds[body] = color;
  }

  if (player < 0 || spider < 0) {
    return;
  }
  const int d = abs(sr[0x20 + player] - sr[0x20 + spider]) + abs(sr[0x10 + player] - sr[0x10 + spider]);
  if (d >= SPIDER_RANGE) {
    return;
  }
  const int amount = (SPIDER_RANGE - d) * 255 / SPIDER_RANGE;
  const int half = NUM_LEDS / 2;
  for (int i = 0; i < NUM_LEDS; i++) {
    nblend(leds[i], CRGB::Red, amount * abs(i - half) / half);
  }
}
#endif
