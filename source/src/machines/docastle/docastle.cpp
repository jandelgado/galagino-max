// ============================================================================
// machines/docastle/docastle.cpp
//
// Mr. Do's Castle (Universal 1983), MAME 0.289 universal/docastle.cpp.
// Main and slave Z80 run in lockstep, one instruction at a time, the one
// behind in cycles first: the latch handshake needs it (both CPUs LDIR
// through the latch, a byte every 21 cycles).
// ============================================================================
#include "../../catalog.h"
#ifdef GAME_DOCASTLE

#include "docastle.h"
#include "../../emulation/input.h"
#include "../../emulation/audio_mix.h"
#include "docastle_main_rom.h"
#include "docastle_slave_rom.h"
#include "docastle_tiles.h"
#include "docastle_sprites.h"
#include "docastle_palette.h"

// Z80 opcode cycle table (static in Tables.h). A host test including
// Z80.c already has it in the same translation unit.
#ifndef DOCASTLE_Z80_TABLES_INCLUDED
#include "../../cpus/z80/Tables.h"
#endif

static const uint8_t MAIN_CPU = 0, SLAVE_CPU = 1;

// main_map
static const uint16_t MAIN_ROM_END = 0x8000;
static const uint16_t SPRITE_END = 0x9a00;  // RAM 8000-97ff, sprites 9800-99ff
static const uint16_t LATCH = 0xa000;       // a000-a7ff, both CPUs
static const uint16_t LATCH_END = 0xa800;
static const uint16_t VIDEO_BASE = 0xb000;  // b000-b7ff, mirror 0x0800
static const uint16_t VIDEO_END = 0xc000;
static const uint16_t VIDEO_MIRROR = 0x07ff;
static const uint16_t SLAVE_NMI = 0xe000;

// sub_map
static const uint16_t SLAVE_ROM_END = 0x4000;
static const uint16_t SLAVE_RAM = 0x8000;
static const uint16_t SLAVE_RAM_END = 0x8800;
static const uint16_t INPUTS = 0xc000;  // c000-c007, c080-c087
static const uint16_t INPUT_MASK = 0xff78;
static const uint16_t SOUND = 0xe000;   // e000/e400/e800/ec00: SN76489A 1-4

// CRTC (table at main 0x0017): 32 rows of 8 lines + 8 adjust = 264 lines,
// 312 pixel clocks (9.828 MHz / 2) a line: 254 cycles of the 4 MHz Z80s.
static const int LINES = 264;
static const int LINE_CYCLES = 254;
static const int VBLANK_LINE = 192;  // MAME screen_update
static const int VSYNC_LINE = 224;   // R7 = 28 rows: main IRQ0
// slave IRQ on each rising edge of MA6 at hsync (docastle_tint): MA =
// 0x80 + 32 * row + 34 (R13, R1, R2) has bit 6 rising at rows 1, 5, .. 29
static const int SLAVE_IRQ_FIRST = 8;
static const int SLAVE_IRQ_PERIOD = 32;

// SN76489A READY is low for 32 chip clocks after a write: slave WAIT
static const int SN_WAIT_CYCLES = 32;

static const uint8_t OP_EI = 0xfb, OP_ED = 0xed, OP_LDI = 0xa0, OP_LDIR = 0xb0;
static const int MIN_STEP_CYCLES = 4;

// TMS1025 ports by select (read_port1..7 callbacks), active low
static const uint8_t SEL_DSW2 = 1, SEL_DSW1 = 2, SEL_JOYS = 3, SEL_BUTTONS = 5, SEL_SYSTEM = 7;
static const uint8_t JOY_RIGHT = 0x01, JOY_UP = 0x02, JOY_LEFT = 0x04, JOY_DOWN = 0x08;
static const uint8_t BTN_FIRE = 0x01, BTN_START1 = 0x08;
static const uint8_t SYS_COIN1 = 0x20;

// panel layout, see docastle.h
static const int FIRST_STRIP = 3;   // strips 3-32 show native x 247..8
static const int LAST_STRIP = 32;
static const int COL_OFFSET = 16;
static const int SCREEN_H = 192;    // native visible y
static const int TILE_ROW0 = 4;     // tilemap scrolldy -32
static const int TILE_COLS = 32;
static const int SPRITE_Y_OFS = 32;
static const uint8_t SPRITE_FLIPX = 0x40, SPRITE_FLIPY = 0x80, SPRITE_COLOR = 0x1f;
static const uint8_t TILE_CODE_HI = 0x20, TILE_COLOR = 0x1f;
static const uint8_t PEN_FG = 8, PEN_MASK = 15;

// game state in main RAM (0 attract, 1 credit, 4 playing), from MAME RAM diffs
static const uint16_t RAM_GAME_STATE = 0x9249 - 0x8000;
static const uint8_t STATE_CREDIT = 1;

docastle::docastle()
  : docastle_main_rom(docastle_main_rom_blob), docastle_slave_rom(docastle_slave_rom_blob),
    docastle_tiles(docastle_tiles_blob), docastle_sprites(docastle_sprites_blob) {
  main_rom = docastle_main_rom.data();
  slave_rom = docastle_slave_rom.data();
  tiles = docastle_tiles.data();
  sprite_gfx = docastle_sprites.data();
}

void docastle::reset() {
  machineBase::reset();
  latch = 0;
  main_wait = latch_fresh = slave_nmi = main_irq = slave_irq = false;
  main_carry = slave_carry = 0;
  mux_select = 0;
  mux_data = 0;
}

// ── CPUs ──

unsigned char GAME_IRAM docastle::opZ80(unsigned short Addr) {
  if (current_cpu == MAIN_CPU && Addr < MAIN_ROM_END) {
    return main_rom[Addr];
  }
  if (current_cpu == SLAVE_CPU && Addr < SLAVE_ROM_END) {
    return slave_rom[Addr];
  }
  return rdZ80(Addr);
}

unsigned char GAME_IRAM docastle::rdZ80(unsigned short Addr) {
  if (current_cpu == SLAVE_CPU) {
    if (Addr < SLAVE_ROM_END) {
      return slave_rom[Addr];
    }
    if (Addr >= SLAVE_RAM && Addr < SLAVE_RAM_END) {
      return memory[DOCASTLE_MEM_SLAVE_RAM + (Addr - SLAVE_RAM)];
    }
    if (Addr >= LATCH && Addr < LATCH_END) {
      main_wait = false;  // sub_from_main_r
      return latch;
    }
    if ((Addr & INPUT_MASK) == INPUTS) {
      return readInputs(Addr & 0xff);
    }
    return 0xff;
  }

  if (Addr < MAIN_ROM_END) {
    return main_rom[Addr];
  }
  if (Addr < SPRITE_END) {
    return memory[DOCASTLE_MEM_MAIN_RAM + (Addr - MAIN_ROM_END)];
  }
  if (Addr >= LATCH && Addr < LATCH_END) {
    latch_fresh = false;  // the WAIT part runs before the step, latchReadBlocks()
    return latch;
  }
  if (Addr >= VIDEO_BASE && Addr < VIDEO_END) {
    return memory[DOCASTLE_MEM_VIDEO + (Addr & VIDEO_MIRROR)];
  }
  return 0xff;
}

void GAME_IRAM docastle::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == SLAVE_CPU) {
    if (Addr >= SLAVE_RAM && Addr < SLAVE_RAM_END) {
      memory[DOCASTLE_MEM_SLAVE_RAM + (Addr - SLAVE_RAM)] = Value;
      return;
    }
    if (Addr >= LATCH && Addr < LATCH_END) {
      latch = Value;  // sub_to_main_w
      latch_fresh = true;
      main_wait = false;
      return;
    }
    if (Addr >= SOUND) {
      sn[(Addr >> 10) & 3].write(Value);
      sn_wait = SN_WAIT_CYCLES;
    }
    return;  // c000-c087 writes: flipscreen, upright cabinet only
  }

  if (Addr < MAIN_ROM_END) {
    return;
  }
  if (Addr < SPRITE_END) {
    memory[DOCASTLE_MEM_MAIN_RAM + (Addr - MAIN_ROM_END)] = Value;
    return;
  }
  if (Addr >= LATCH && Addr < LATCH_END) {
    latch = Value;  // main_to_sub_w: WAIT until the slave reads it
    latch_fresh = false;
    main_wait = true;
    return;
  }
  if (Addr >= VIDEO_BASE && Addr < VIDEO_END) {
    memory[DOCASTLE_MEM_VIDEO + (Addr & VIDEO_MIRROR)] = Value;
    return;
  }
  if (Addr == SLAVE_NMI) {
    slave_nmi = true;
  }
}

// TMS1025 port data for a select value, both 4 bit halves at once
uint8_t docastle::inputPort(uint8_t select) {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xff;
  switch (select) {
  case SEL_DSW2:
    return DOCASTLE_DSW2;
  case SEL_DSW1:
    return DOCASTLE_DSW1;
  case SEL_JOYS:
    if (keys & BUTTON_RIGHT) {
      v &= ~JOY_RIGHT;
    }
    if (keys & BUTTON_UP) {
      v &= ~JOY_UP;
    }
    if (keys & BUTTON_LEFT) {
      v &= ~JOY_LEFT;
    }
    if (keys & BUTTON_DOWN) {
      v &= ~JOY_DOWN;
    }
    return v;
  case SEL_BUTTONS:
    if (keys & BUTTON_FIRE) {
      v &= ~BTN_FIRE;
    }
    if (keys & BUTTON_START) {
      v &= ~BTN_START1;
    }
    return v;
  case SEL_SYSTEM:
    if (keys & BUTTON_COIN) {
      v &= ~SYS_COIN1;
    }
    return v;
  default:
    return 0;  // unconnected ports 4 and 6
  }
}

// inputs_flipscreen_r: data of the port the previous read selected, then
// latch this address as the new select (select 0 keeps the data)
uint8_t docastle::readInputs(uint8_t offset) {
  if (mux_select != 0) {
    mux_data = inputPort(mux_select);
  }
  mux_select = offset & 7;
  return mux_data;
}

// main_from_sub_r: a read asserts WAIT, the slave's next latch access ends
// it and the read is retried. Both CPUs LDIR through the latch (main 0x00df
// reads, slave 0x007d writes), a byte every 21 cycles. This lockstep orders
// whole instructions, not bus cycles, so a read waits for a byte the slave
// wrote since main's last access: no byte is read twice or skipped. The
// game reads the latch only by LDIR, the one opcode checked before a step.
bool GAME_IRAM docastle::latchReadBlocks() {
  const Z80 &z = cpu[MAIN_CPU];
  if (main_rom[z.PC.W] != OP_ED || z.PC.W >= MAIN_ROM_END - 1) {
    return false;
  }
  const uint8_t op = main_rom[z.PC.W + 1];
  if ((op != OP_LDIR && op != OP_LDI) || z.HL.W < LATCH || z.HL.W >= LATCH_END) {
    return false;
  }
  main_wait = !latch_fresh;
  return main_wait;
}

// One instruction; returns its cycles. ICount starts at minus the opcode's
// cycles, so block opcodes (LDIR) do one pass per step, StepZ80 adds the
// prefixed opcode cycles.
static int stepZ80(Z80 &z, uint8_t op) {
  z.ICount = -Cycles[op];
  StepZ80(&z);
  if (op == OP_EI) {
    z.ICount = -Cycles[op];  // EI parks ICount at 1 for RunZ80
  }
  const int used = -z.ICount;
  return used > MIN_STEP_CYCLES ? used : MIN_STEP_CYCLES;
}

int GAME_IRAM docastle::stepMain() {
  Z80 &z = cpu[MAIN_CPU];
  current_cpu = MAIN_CPU;
  if (main_irq && (z.IFF & IFF_1)) {
    IntZ80(&z, INT_RST38);
    main_irq = false;
    game_started = 1;
  }
  if (latchReadBlocks()) {
    return 0;
  }
  return stepZ80(z, opZ80(z.PC.W));
}

int GAME_IRAM docastle::stepSlave() {
  Z80 &z = cpu[SLAVE_CPU];
  current_cpu = SLAVE_CPU;
  if (slave_nmi) {
    IntZ80(&z, INT_NMI);
    slave_nmi = false;
  }
  if (slave_irq && (z.IFF & IFF_1)) {
    IntZ80(&z, INT_RST38);  // HOLD_LINE: until accepted
    slave_irq = false;
  }
  sn_wait = 0;
  const int used = stepZ80(z, opZ80(z.PC.W));
  return used + sn_wait;
}

void docastle::run_frame(void) {
  int main_t = main_carry, slave_t = slave_carry;  // cycles into the frame

  for (int line = 0; line < LINES; line++) {
    if (line == VBLANK_LINE) {
      vblank.publish([this](VideoState &v) {
        memcpy(v.video, memory + DOCASTLE_MEM_VIDEO, TILE_BYTES);
        memcpy(v.color, memory + DOCASTLE_MEM_COLOR, TILE_BYTES);
        memcpy(v.sprites, memory + DOCASTLE_MEM_SPRITES, SPRITE_BYTES);
      });
    }
    if (line == VSYNC_LINE) {
      main_irq = true;  // held until accepted
    }
    if (line % SLAVE_IRQ_PERIOD == SLAVE_IRQ_FIRST) {
      slave_irq = true;
    }

    // lockstep: the CPU behind runs; main in WAIT stalls behind the slave
    const int end = (line + 1) * LINE_CYCLES;
    while (main_t < end || slave_t < end) {
      if (main_t <= slave_t ? main_t < end : slave_t >= end) {
        if (main_wait) {
          main_t = slave_t < end ? slave_t + 1 : end;
          continue;
        }
        main_t += stepMain();
      } else {
        slave_t += stepSlave();
      }
    }
  }
  main_carry = main_t - LINES * LINE_CYCLES;  // last opcode overshoot
  slave_carry = slave_t - LINES * LINE_CYCLES;
  current_cpu = MAIN_CPU;
}

// ── video core ──

// MAME draws offs 0x1fc first, 0 last (on top): keep that order
void docastle::prepare_frame(void) {
  vblank.read(video);

  active_sprites = 0;
  for (int offs = SPRITE_BYTES - 4; offs >= 0; offs -= 4) {
    const uint8_t *sr = video.sprites + offs;
    const int sy = sr[0] - SPRITE_Y_OFS;
    if (sy <= -16 || sy >= SCREEN_H) {
      continue;
    }
    sprite_S &s = sprite[active_sprites++];
    s.x = ((sr[1] + 8) & 0xff) - 8;
    s.y = sy;
    s.code = sr[3];
    s.color = sr[2] & SPRITE_COLOR;
    s.flip_x = (sr[2] & SPRITE_FLIPX) != 0;
    s.flip_y = (sr[2] & SPRITE_FLIPY) != 0;
  }
}

// Strip k covers native x 8 * (33 - k) + 7 - r for row r: one tile column.
// romconv stores tiles rotated, so gfx row r is that x, nibble c is y & 7.
void docastle::renderTiles(int strip) {
  const int tc = 33 - strip;

  for (int tr = TILE_ROW0; tr < TILE_ROW0 + SCREEN_H / 8; tr++) {
    const int t = tr * TILE_COLS + tc;
    const uint8_t color = video.color[t];
    const uint32_t *gfx = tiles[video.video[t] + ((color & TILE_CODE_HI) ? 256 : 0)];
    const uint16_t *pal = docastle_palette + (color & TILE_COLOR) * 8;
    const int y0 = (tr - TILE_ROW0) * 8;

    for (int r = 0; r < 8; r++) {
      const uint32_t bits = gfx[r];
      uint16_t *dst = frame_buffer + r * OUT_WIDTH + COL_OFFSET + y0;
      uint8_t *fgp = fg[r] + y0;
      for (int c = 0; c < 8; c++) {
        const uint8_t pen = (bits >> (4 * c)) & 15;
        dst[c] = pal[pen & 7];
        fgp[c] = pen >= PEN_FG;
      }
    }
  }
}

// MAME draw_sprites with the priority bitmap: pens 8-14 draw, pen 15 draws
// only over an earlier sprite, every opaque pen takes the pixel. Tile pens
// 8-15 are drawn over all sprites, so those pixels are skipped here.
void docastle::renderSprites(int strip) {
  memset(covered, 0, sizeof(covered));
  const int x_last = 8 * (33 - strip) + 7;  // native x of strip row 0

  for (int i = 0; i < active_sprites; i++) {
    const sprite_S &s = sprite[i];
    if (x_last - 7 > s.x + 15 || x_last < s.x) {
      continue;
    }
    const uint32_t (*gfx)[2] = sprite_gfx[s.code];
    const uint16_t *pal = docastle_palette + s.color * 8;

    for (int r = 0; r < 8; r++) {
      const int xi = x_last - r - s.x;
      if (xi < 0 || xi > 15) {
        continue;
      }
      const uint32_t *bits = gfx[s.flip_x ? xi : 15 - xi];
      if (!(bits[0] | bits[1])) {
        continue;
      }
      uint16_t *dst = frame_buffer + r * OUT_WIDTH + COL_OFFSET;
      for (int yi = 0; yi < 16; yi++) {
        const int y = s.y + yi;
        if (y < 0 || y >= SCREEN_H) {
          continue;
        }
        const int n = s.flip_y ? 15 - yi : yi;
        const uint8_t pen = (bits[n >> 3] >> (4 * (n & 7))) & 15;
        if (pen < PEN_FG) {
          continue;
        }
        const bool draw = pen != PEN_MASK || covered[r][y];
        covered[r][y] = 1;
        if (draw && !fg[r][y]) {
          dst[y] = pal[pen & 7];
        }
      }
    }
  }
}

void docastle::render_row(short row) {
  if (row < FIRST_STRIP || row > LAST_STRIP) {
    memset(frame_buffer, 0, OUT_WIDTH * 8 * sizeof(uint16_t));
    return;
  }
  // columns outside the 192 native lines stay black
  for (int r = 0; r < 8; r++) {
    memset(frame_buffer + r * OUT_WIDTH, 0, COL_OFFSET * sizeof(uint16_t));
    memset(frame_buffer + r * OUT_WIDTH + COL_OFFSET + SCREEN_H, 0,
           (OUT_WIDTH - COL_OFFSET - SCREEN_H) * sizeof(uint16_t));
  }
  renderTiles(row);
  renderSprites(row);
}

// ── audio, video core ──

// 4 x SN76489A at 0.25 each in MAME: sum and clip. A tone at full volume
// is +-90 here; the game rarely plays more than 4 at once.
void docastle::renderAudio(int16_t *out, int n) {
  Sn76489::renderBank(sn, out, n);
  audioClampAll(out, n, AUDIO_MAX);
}

#ifdef LED_PIN
// logo colors (quantized logos/docastle.png): red, blue, yellow, pale yellow
static const CRGB DOCASTLE_LED_RED(0xd90008), DOCASTLE_LED_BLUE(0x0000fb);
static const CRGB DOCASTLE_LED_YELLOW(0xffd67d), DOCASTLE_LED_PALE(0xffff8a);

static const uint8_t BLINK_FRAMES = 16;

void docastle::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      DOCASTLE_LED_RED, DOCASTLE_LED_BLUE, DOCASTLE_LED_YELLOW, DOCASTLE_LED_PALE,
      DOCASTLE_LED_YELLOW, DOCASTLE_LED_BLUE, DOCASTLE_LED_RED};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// blinks yellow while a credit waits for START
void docastle::gameLeds(CRGB *leds) {
  static uint8_t blink = 0;
  blink++;
  if (memory[DOCASTLE_MEM_MAIN_RAM + RAM_GAME_STATE] == STATE_CREDIT && (blink & BLINK_FRAMES)) {
    fill_solid(leds, NUM_LEDS, DOCASTLE_LED_YELLOW);
    return;
  }
  menuLeds(leds);
}
#endif

#endif
