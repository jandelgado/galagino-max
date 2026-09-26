#include "millipede.h"
#include "millipede_rom.h"
#include "millipede_gfx.h"
#include "../../emulation/pokey.h"
#include "../../emulation/arena.h"

static const uint16_t PLANE = 0x800; // MSB plane offset in gfx

// memory map (MAME milliped_map), 15 bit address bus
enum : uint16_t {
  ADDR_MASK = 0x7fff,
  WORK_END = 0x0400,
  POKEY1 = 0x0400, // ALLPOT reads DSW1
  POKEY2 = 0x0800, // ALLPOT reads DSW2
  VIDEO_BASE = 0x1000,
  VIDEO_END = 0x1400,
  IN0 = 0x2000,
  IN1 = 0x2001,
  IN2 = 0x2010,
  IN3 = 0x2011,
  EAROM_READ = 0x2030,
  PALETTE_BASE = 0x2480, // 0x2480-0x249f
  PALETTE_SIZE = 0x20,
  OUTLATCH = 0x2500,     // 0x2500-0x2507, data bit 7
  IRQ_ACK = 0x2600,
  EAROM_CONTROL = 0x2700,
  EAROM_WRITE = 0x2780,  // 0x2780-0x27bf: address + data
  EAROM_SIZE = 0x40,
  ROM_BASE = 0x4000,
  REG_BLOCK_MASK = 0xfff0, // POKEYs decode 16 registers
};

// POKEY register offsets beyond AUDF1..AUDC4 (0-7)
enum : uint8_t { POKEY_AUDCTL = 0x08, POKEY_ALLPOT = 0x08, POKEY_RANDOM = 0x0a, POKEY_SKCTL = 0x0f };
static const uint8_t AUDCTL_POLY9 = 0x80;
static const uint8_t SK_RESET = 0x03; // SKCTL bits, both clear = reset

static const uint8_t OUTLATCH_TBEN = 5;  // latch bit, LS259 D input
static const uint8_t OUTLATCH_DATA = 0x80; // data bus bit 7

// input bits, active low unless noted
enum : uint8_t {
  IN0_FIRE = 0x10,
  IN0_START1 = 0x20,
  IN0_VBLANK = 0x40, // active high
  IN1_IDLE = 0x70,   // P2 fire/start, unused bit 6
  IN2_RIGHT = 0x01,
  IN2_LEFT = 0x02,
  IN2_DOWN = 0x04,
  IN2_UP = 0x08,
  IN2_COIN1 = 0x20,
  IN3_IDLE = 0xff, // P2 stick idle, upright, test switch off
};

// raster timing: IRQ every 32 lines, vblank from line 240, 16 px letterbox
static const int IRQ_STEP = 16;
static const int VBLANK_LINE = 240;
static const int SPRITE_CLIP_X = 248;
static const int TOP_BORDER = 16;

static inline uint8_t gfx_pen(const unsigned char *gfx, uint16_t row, int x) {
  return (((gfx[PLANE + row] << x) & 0x80) >> 6) | (((gfx[row] << x) & 0x80) >> 7);
}

// MAME milliped_set_color: inverted RGB 3-3-2 with resistor weights
static uint16_t millipede_color(uint8_t d) {
  const uint8_t n = ~d;
  const uint8_t r = 0x21 * ((n >> 5) & 1) + 0x47 * ((n >> 6) & 1) + 0x97 * ((n >> 7) & 1);
  const uint8_t g = 0x47 * ((n >> 3) & 1) + 0x97 * ((n >> 4) & 1);
  const uint8_t b = 0x21 * (n & 1) + 0x47 * ((n >> 1) & 1) + 0x97 * ((n >> 2) & 1);
  uint16_t rgb = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (rgb >> 8) | (rgb << 8);
}

millipede::millipede() {
  memset(&m_cpu, 0, sizeof(m_cpu));
  m_cpu.read = main_read;
  m_cpu.write = main_write;
  m_cpu.user = this;
  rom = millipede_rom.data();
  gfx = millipede_gfx.data();
}

millipede::~millipede() {
  millipede_rom.release();
  millipede_gfx.release();
}

void millipede::start() {
  if (tile_cols) {
    return; // start() runs twice
  }

  tile_cols = Arena::alloc<uint16_t>(128 * 8);
  sprite_cols = Arena::alloc<uint32_t>(128 * 2 * 8);

  // tiles: gfx codes 0x40..0x7f (bank 0) and 0xc0..0xff (bank 1), MAME
  // milliped_get_tile_info; no per-tile flip
  for (int t = 0; t < 128; t++) {
    const int code = 0x40 + (t & 0x3f) + (t >> 6) * 0x80;
    for (int cx = 0; cx < 8; cx++) {
      uint16_t w = 0;
      for (int i = 0; i < 8; i++) {
        w |= gfx_pen(gfx, code * 8 + i, cx) << (2 * i);
      }
      tile_cols[t * 8 + cx] = w;
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

void millipede::reset() {
  machineBase::reset();
  frame_cycles = total_cycles = 0;
  memset(random_base, 0, sizeof(random_base));
  memset(allpot, 0, sizeof(allpot));
  in_vblank = false;
  tben = false;
  memset(palette_ram, 0, sizeof(palette_ram));
  earom.reset();
  m6502_reset(&m_cpu);
}

// low nibble: P8 DIPs while TBEN is low, else the idle trackball counter (0)
uint8_t millipede::in0() const {
  const uint8_t k = input->buttons_get();
  uint8_t v = IN0_FIRE | IN0_START1 | (in_vblank ? IN0_VBLANK : 0) | (tben ? 0 : MILLIPEDE_P8_IN0);
  if (k & BUTTON_FIRE) {
    v &= ~IN0_FIRE;
  }
  if (k & BUTTON_START) {
    v &= ~IN0_START1;
  }
  return v;
}

uint8_t millipede::in1() const {
  return IN1_IDLE | (tben ? 0 : MILLIPEDE_P8_IN1);
}

uint8_t millipede::in2() const {
  const uint8_t k = input->buttons_get();
  uint8_t v = 0xff;
  if (k & BUTTON_RIGHT) {
    v &= ~IN2_RIGHT;
  }
  if (k & BUTTON_LEFT) {
    v &= ~IN2_LEFT;
  }
  if (k & BUTTON_DOWN) {
    v &= ~IN2_DOWN;
  }
  if (k & BUTTON_UP) {
    v &= ~IN2_UP;
  }
  if (k & BUTTON_COIN) {
    v &= ~IN2_COIN1;
  }
  return v;
}

bool millipede::pokey_running(int n) const {
  return soundregs[n * Pokey::REG_COUNT + Pokey::REG_SKCTL] & SK_RESET;
}

// ALLPOT (DIPs) and RANDOM are the only POKEY reads the game needs
uint8_t millipede::pokey_read(int n, uint8_t reg) {
  if (reg == POKEY_ALLPOT) {
    // MAME pokey ALLPOT: latched value while in reset, else the pots (DSW)
    if (pokey_running(n)) {
      allpot[n] = n ? MILLIPEDE_DSW2 : MILLIPEDE_DSW1;
    }
    return allpot[n];
  }
  if (reg == POKEY_RANDOM) {
    // polys hold at 0 in reset and restart on release, see centipede.cpp
    const bool poly9 = soundregs[n * Pokey::REG_COUNT + Pokey::REG_AUDCTL] & AUDCTL_POLY9;
    const uint32_t clock = pokey_running(n) ? cpu_clock() - random_base[n] : 0;
    return Pokey::random(clock, poly9);
  }
  return 0;
}

// AUDF1..AUDC4, AUDCTL, SKCTL go to soundregs (layout in pokey.h)
void millipede::pokey_write(int n, uint8_t reg, uint8_t v) {
  uint8_t *regs = soundregs + n * Pokey::REG_COUNT;
  if (reg <= POKEY_AUDCTL) {
    regs[reg] = v;
  } else if (reg == POKEY_SKCTL) {
    if (!pokey_running(n) && (v & SK_RESET)) {
      random_base[n] = cpu_clock();
    }
    regs[Pokey::REG_SKCTL] = v;
  }
}

uint8_t millipede::main_read(m6502_t *cpu, uint16_t a) {
  millipede *s = static_cast<millipede *>(cpu->user);
  a &= ADDR_MASK;
  if (a >= ROM_BASE) {
    return s->rom[a - ROM_BASE];
  }
  if (a < WORK_END) {
    return s->memory[a];
  }
  if (a >= VIDEO_BASE && a < VIDEO_END) {
    return s->memory[VIDEO_RAM + (a - VIDEO_BASE)];
  }
  switch (a & REG_BLOCK_MASK) {
    case POKEY1: return s->pokey_read(0, a & 0x0f);
    case POKEY2: return s->pokey_read(1, a & 0x0f);
  }

  switch (a) {
    case IN0: return s->in0();
    case IN1: return s->in1();
    case IN2: return s->in2();
    case IN3: return IN3_IDLE;
    case EAROM_READ: return s->earom.read();
  }
  return 0; // 0x3000-0x3fff: empty ROM socket
}

void millipede::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  millipede *s = static_cast<millipede *>(cpu->user);
  a &= ADDR_MASK;
  if (a < WORK_END) {
    s->memory[a] = v;
    return;
  }
  if (a >= VIDEO_BASE && a < VIDEO_END) {
    s->memory[VIDEO_RAM + (a - VIDEO_BASE)] = v;
    return;
  }
  switch (a & REG_BLOCK_MASK) {
    case POKEY1: s->pokey_write(0, a & 0x0f, v); return;
    case POKEY2: s->pokey_write(1, a & 0x0f, v); return;
  }

  if (a >= PALETTE_BASE && a < PALETTE_BASE + PALETTE_SIZE) {
    s->palette_ram[a - PALETTE_BASE] = v;
    return;
  }
  if (a >= EAROM_WRITE && a < EAROM_WRITE + EAROM_SIZE) {
    s->earom.latch(a, v);
    return;
  }
  // outlatch: only TBEN matters; VIDROT (flip) and CNTRLSEL (P2 controls)
  // stay off on an upright one player setup, coin counters and LEDs ignored
  if (a == OUTLATCH + OUTLATCH_TBEN) {
    s->tben = v & OUTLATCH_DATA;
    return;
  }
  if (a == EAROM_CONTROL) {
    s->earom.control(v);
    return;
  }
  if (a == IRQ_ACK) {
    cpu->irq = 0;
  }
  // 0x2680 watchdog: ignored
}

uint32_t millipede::cpu_clock() const {
  return total_cycles + frame_cycles;
}

// cycle: target cycle count within the frame
void millipede::run_until(uint32_t cycle) {
  while (frame_cycles < cycle) {
    frame_cycles += m6502_step(&m_cpu);
  }
}

void millipede::run_frame() {
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

void millipede::prepare_frame() {
  vblank.read(video);

  // palette 0x00-0x0f: 4 tile colour banks, 0x10-0x1f: 4 sprite colour banks
  uint16_t sprite_pal[16];
  for (int i = 0; i < 16; i++) {
    tile_pal[i] = millipede_color(video.palette[i]);
    sprite_pal[i] = millipede_color(video.palette[16 + i]);
  }

  // sprite colour byte: bits 7-6 bank, bits 5-4/3-2/1-0 colour of pens
  // 3/2/1, colour 0 transparent (MAME milliped_set_color, init_penmask)
  const uint8_t *sr = video.ram + SPRITE_OFS;
  for (int i = 0; i < 16; i++) {
    const uint8_t b = sr[i], color = sr[0x30 + i];
    Sprite &s = spr[i];
    const int code = ((b & 0x3e) >> 1) | ((b & 1) << 6);
    s.base = (code * 2 + (b >> 7)) * 8;
    s.x = sr[0x20 + i];
    s.y = 240 - sr[0x10 + i];
    s.opaque = 0;
    const uint16_t *pal = sprite_pal + (color >> 6) * 4;
    for (int p = 1; p < 4; p++) {
      const uint8_t sel = (color >> (2 * (p - 1))) & 3;
      s.color[p] = pal[sel];
      if (sel) {
        s.opaque |= 1 << p;
      }
    }
  }
}

// ROT270: output line py shows native column x = 255 - py, output
// column px = native y (as centipede)
void millipede::render_line(uint16_t *line, int x) const {
  const int tx = x >> 3, cx = x & 7;
  for (int ty = 0; ty < 30; ty++) {
    // bit 6: gfx bank, bits 7-6: colour bank
    const uint8_t d = video.ram[ty * 32 + tx];
    const uint16_t *pal = tile_pal + (d >> 6) * 4;
    uint16_t w = tile_cols[(((d >> 6) & 1) << 6 | (d & 0x3f)) * 8 + cx];
    uint16_t *p = line + ty * 8;
    for (int i = 0; i < 8; i++, w >>= 2) {
      p[i] = pal[w & 3];
    }
  }

  if (x >= SPRITE_CLIP_X) {
    return;
  }

  // sprites 0..15, later ones on top; no flipx (VIDROT off)
  for (int i = 0; i < 16; i++) {
    const Sprite &s = spr[i];
    const int c = x - s.x;
    if ((unsigned)c >= 8) {
      continue;
    }
    uint32_t w = sprite_cols[s.base + c];
    for (int k = 0, y = s.y; k < 16; k++, y++, w >>= 2) {
      const uint8_t pen = w & 3;
      if (((s.opaque >> pen) & 1) && (unsigned)y < VBLANK_LINE) {
        line[y] = s.color[pen];
      }
    }
  }
}

void millipede::render_row(short strip) {
  for (int oy = 0; oy < 8; oy++) {
    const int py = strip * 8 + oy - TOP_BORDER; // 256 rows centered in 288
    if (py < 0 || py >= 256) {
      continue;
    }
    render_line(frame_buffer + oy * 240, 255 - py);
  }
}
