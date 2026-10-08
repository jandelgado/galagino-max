#ifdef ENABLE_PHOZON

#include "phozon.h"

static_assert(PHOZON_SUB_RAM_OFF + PHOZON_SUB_RAM_SIZE <= RAMSIZE, "phozon RAM exceeds memory[]");

static const uint16_t SHARED_RAM_END = 0x2000;    // main/sub video + work RAM
static const uint16_t WSG_REGS = 0x40;            // 15XX registers at shared RAM start
static const uint16_t ATTR_OFF = 0x400;           // tile attributes after tile codes

// sprite RAM banks MAME phozon_draw_sprites reads (main CPU addresses)
static const uint16_t SPRITE_BANK1 = 0x0F80;      // code, color
static const uint16_t SPRITE_BANK2 = 0x1780;      // y, x
static const uint16_t SPRITE_BANK3 = 0x1F80;      // flags, enable / x msb

// idle loops, left only through the CPU's own vblank IRQ, which sets the
// polled flag (MAME trace + write taps): stepping them is wasted
static const uint16_t MAIN_IDLE_PC = 0x8029;      // LDA $0827 / BEQ $8029
static const uint16_t MAIN_VBLANK_FLAG = 0x0827;
static const uint16_t SUB_IDLE_PC = 0xE171;       // LDA $0828 / BEQ $E171
static const uint16_t SUB_VBLANK_FLAG = 0x0828;
static const uint16_t SOUND_IDLE_PC = 0xE0B5;     // BRA $E0B5
static const uint16_t IDLE_BEQ_OFS = 3;           // BEQ after the 3 byte LDA

// slices of 4 instructions over a 2 instruction loop: a slice can end on
// either instruction, depending on where the loop was entered
static inline bool in_poll_loop(uint16_t pc, uint16_t loop) {
  return (uint16_t)(pc - loop) <= IDLE_BEQ_OFS;
}

static const short WIDTH = 224;                   // frame_buffer row stride
static const int16_t SPRITE_X_OFFSET = 69;        // MAME: sx = x - 69

phozon::phozon()
  : phozon_rom_main(phozon_rom_main_blob),
    phozon_rom_sound(phozon_rom_sound_blob),
    phozon_rom_sub(phozon_rom_sub_blob),
    phozon_tilemap(phozon_tilemap_blob),
    phozon_sprites(phozon_sprites_blob),
    rom_main(phozon_rom_main.data()),
    rom_sound(phozon_rom_sound.data()),
    rom_sub(phozon_rom_sub.data()),
    tiles(phozon_tilemap.data()),
    sprites(phozon_sprites.data()),
    n_blocks(0) { }

void phozon::reset() {
  machineBase::reset();

  // power-on: mainlatch all 0, so sound/sub CPU and namcoio held in reset
  main_irq_mask = sound_irq_mask = sub_irq_mask = 0;
  wsg_enable = 0;
  io_reset = sound_reset = sub_reset = 1;
  dipmux_sel = 0;

  io[0].reset();
  io[1].reset();

  m6809_reset(&main_cpu);
  m6809_reset(&sound_cpu);
  m6809_reset(&sub_cpu);
  install_rom_direct();
}

// m6809_reset() clears the window: reinstall after every reset
void phozon::install_rom_direct(void) {
  main_cpu.rom_direct = rom_main;
  main_cpu.rom_base = 0x8000;
  main_cpu.rom_size = 0x8000;
  sound_cpu.rom_direct = rom_sound;
  sound_cpu.rom_base = 0xE000;
  sound_cpu.rom_size = 0x2000;
  sub_cpu.rom_direct = rom_sub;
  sub_cpu.rom_base = 0xE000;
  sub_cpu.rom_size = 0x2000;
}

// ============================================================================
// Mainlatch LS259 (main 0x5000-0x500f, A0 = data)
// ============================================================================

void phozon::mainlatch_w(uint16_t addr) {
  uint8_t v = addr & 1;

  switch ((addr >> 1) & 7) {
    case 0:   // mask -> 0 acknowledges a pending IRQ (CLEAR_LINE in MAME)
      sound_irq_mask = v;
      if (!v) {
        sound_cpu.irq_pending = 0;
      }
      break;
    case 1:
      main_irq_mask = v;
      if (!v) {
        main_cpu.irq_pending = 0;
      }
      break;
    case 2:
      sub_irq_mask = v;
      if (!v) {
        sub_cpu.irq_pending = 0;
      }
      break;
    case 3:
      wsg_enable = v;
      break;
    case 4:   // entering reset clears both chips
      if (!v && !io_reset) {
        io[0].reset();
        io[1].reset();
      }
      io_reset = !v;
      break;
    case 5:   // leaving reset restarts the CPU from its vector
      if (v && sound_reset) {
        m6809_reset(&sound_cpu);
        install_rom_direct();
      }
      sound_reset = !v;
      break;
    case 6:
      if (v && sub_reset) {
        m6809_reset(&sub_cpu);
        install_rom_direct();
      }
      sub_reset = !v;
      break;
  }
}

// ============================================================================
// Memory map (MAME phozon_cpu1/2/3_map)
// ============================================================================

// 15XX shared RAM: registers live in soundregs[] for the audio renderer
uint8_t IRAM_ATTR phozon::shared_r(uint16_t off) {
  off &= 0x03FF;
  return (off < WSG_REGS) ? soundregs[off] : memory[PHOZON_SHARED_OFF + off];
}

void IRAM_ATTR phozon::shared_w(uint16_t off, uint8_t val) {
  off &= 0x03FF;
  if (off < WSG_REGS) {
    soundregs[off] = val;
    return;
  }
  memory[PHOZON_SHARED_OFF + off] = val;
}

uint8_t IRAM_ATTR phozon::main_r(uint16_t addr) {
  if (addr < SHARED_RAM_END) {
    return memory[addr];
  }
  if ((addr & 0xFC00) == 0x4000) {
    return shared_r(addr);
  }
  if ((addr & 0xFFE0) == 0x4800) {
    return io[(addr >> 4) & 1].read(addr);
  }
  if (addr >= 0x8000) {
    return rom_main[addr - 0x8000];
  }
  return 0xFF;
}

void IRAM_ATTR phozon::main_w(uint16_t addr, uint8_t val) {
  if (addr < SHARED_RAM_END) {
    memory[addr] = val;
    if (!game_started && addr < 0x0800 && val != 0) {
      game_started = 1;
    }
    return;
  }
  if ((addr & 0xFC00) == 0x4000) {
    shared_w(addr, val);
    return;
  }
  if ((addr & 0xFFE0) == 0x4800) {
    io[(addr >> 4) & 1].write(addr, val);
    return;
  }
  if ((addr & 0xFFF0) == 0x5000) {
    mainlatch_w(addr);
  }
  // 0x7000 watchdog: ignored
}

uint8_t IRAM_ATTR phozon::sub_r(uint16_t addr) {
  if (addr < SHARED_RAM_END) {
    return memory[addr];
  }
  if ((addr & 0xFC00) == 0x4000) {
    return shared_r(addr);
  }
  if ((addr & 0xF800) == 0xA000) {
    return memory[PHOZON_SUB_RAM_OFF + (addr & 0x07FF)];
  }
  if (addr >= 0xE000) {
    return rom_sub[addr - 0xE000];
  }
  return 0xFF;
}

void IRAM_ATTR phozon::sub_w(uint16_t addr, uint8_t val) {
  if (addr < SHARED_RAM_END) {
    memory[addr] = val;
    return;
  }
  if ((addr & 0xFC00) == 0x4000) {
    shared_w(addr, val);
    return;
  }
  if ((addr & 0xF800) == 0xA000) {
    memory[PHOZON_SUB_RAM_OFF + (addr & 0x07FF)] = val;
  }
}

unsigned char IRAM_ATTR phozon::m6809_read(m6809_state *s, uint16_t addr) {
  if (s == &main_cpu) {
    return main_r(addr);
  }
  if (s == &sub_cpu) {
    return sub_r(addr);
  }

  // sound CPU: 15XX shared RAM + ROM only
  if (addr < 0x0400) {
    return shared_r(addr);
  }
  if (addr >= 0xE000) {
    return rom_sound[addr - 0xE000];
  }
  return 0xFF;
}

void IRAM_ATTR phozon::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (s == &main_cpu) {
    main_w(addr, val);
    return;
  }
  if (s == &sub_cpu) {
    sub_w(addr, val);
    return;
  }
  if (addr < 0x0400) {
    shared_w(addr, val);
  }
}

unsigned char IRAM_ATTR phozon::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

// ============================================================================
// Frame: fine interleave of the 3 CPUs, then vblank
// ============================================================================

void phozon::run_frame(void) {
  for (int i = 0; i < PHOZON_SLICES; i++) {
    // idle decided per slice: the main CPU can release the others from
    // reset mid-frame. Flag checked too: an RTI can land on the idle PC
    // with the flag already set.
    bool main_idle = in_poll_loop(main_cpu.PC, MAIN_IDLE_PC) && !memory[MAIN_VBLANK_FLAG];
    bool sound_idle = sound_reset || sound_cpu.PC == SOUND_IDLE_PC;
    bool sub_idle = sub_reset || (in_poll_loop(sub_cpu.PC, SUB_IDLE_PC) && !memory[SUB_VBLANK_FLAG]);
    if (main_idle && sound_idle && sub_idle) {
      break;
    }

    if (!main_idle) {
      m6809_step(&main_cpu, 4);
    }
    if (!sound_idle) {
      m6809_step(&sound_cpu, 4);
    }
    if (!sub_idle) {
      m6809_step(&sub_cpu, 4);
    }
  }

  // latch the sprite tables before the vblank IRQ handlers rewrite them
  vblank.publish([this](VideoState &v) {
    memcpy(v.s1, &memory[SPRITE_BANK1], sizeof(v.s1));
    memcpy(v.s2, &memory[SPRITE_BANK2], sizeof(v.s2));
    memcpy(v.s3, &memory[SPRITE_BANK3], sizeof(v.s3));
  });

  // vblank: IRQ to every CPU whose mask is set, then the namcoio run the
  // command left in nibble 8 (MAME: 50us after vblank)
  if (main_irq_mask) {
    m6809_irq(&main_cpu);
  }
  if (sound_irq_mask && !sound_reset) {
    m6809_irq(&sound_cpu);
  }
  if (sub_irq_mask && !sub_reset) {
    m6809_irq(&sub_cpu);
  }

  if (io_reset) {
    return;
  }
  // only the 56XX pin 13 drives the DIP multiplexer
  io[0].run58([this](uint8_t p) { return io_in(0, p); }, [](uint8_t) {});
  io[1].run56([this](uint8_t p) { return io_in(1, p); },
              [this](uint8_t v) { dipmux_sel = v; });
}

// ============================================================================
// Inputs: 4-bit ports, active low (MAME INPUT_PORTS phozon)
// chip 0: 0 = COINS, 1 = P1, 2 = P2, 3 = BUTTONS
// chip 1: 0 = DSW2 via dipmux, 1/2 = DSW1 low/high, 3 = DSW0
// ============================================================================

uint8_t phozon::io_in(uint8_t chip, uint8_t port) {
  if (chip == 1) {
    switch (port) {
      case 0: return (dipmux_sel ? (PHOZON_DSW2 >> 4) : PHOZON_DSW2) & 0x0F;
      case 1: return PHOZON_DSW1 & 0x0F;
      case 2: return (PHOZON_DSW1 >> 4) & 0x0F;
      default: return PHOZON_DSW0 & 0x0F;
    }
  }

  uint8_t keymask = input->buttons_get();
  uint8_t v = 0x0F;

  switch (port) {
    case 0:   // bit0 coin 1
      if (keymask & BUTTON_COIN) {
        v &= ~0x01;
      }
      break;
    case 1:   // 8-way stick: bit0 up, bit1 right, bit2 down, bit3 left
      if (keymask & BUTTON_UP) {
        v &= ~0x01;
      }
      if (keymask & BUTTON_RIGHT) {
        v &= ~0x02;
      }
      if (keymask & BUTTON_DOWN) {
        v &= ~0x04;
      }
      if (keymask & BUTTON_LEFT) {
        v &= ~0x08;
      }
      break;
    case 3:   // bit0 button 1, bit2 start 1
      if (keymask & BUTTON_FIRE) {
        v &= ~0x01;
      }
      if (keymask & BUTTON_START) {
        v &= ~0x04;
      }
      break;
  }
  return v;
}

// ============================================================================
// Video
// ============================================================================

// MAME phozon_draw_sprites, converted to galagino coordinates once per frame
void phozon::prepare_frame(void) {
  static const uint8_t SIZE[4] = { 1, 0, 3, 0 };   // 16, 8, 32, unused

  vblank.read(video);

  n_blocks = 0;
  for (uint8_t offs = 0; offs < SPRITE_TABLE; offs += 2) {
    if (video.s3[offs + 1] & 2) {   // disabled
      continue;
    }

    uint8_t flags = video.s3[offs];
    Block *b = &blocks[n_blocks++];
    b->code = (video.s1[offs] << 2) | (flags >> 6);
    b->color = video.s1[offs + 1] & 0x3F;
    b->flipx = flags & 1;
    b->flipy = (flags >> 1) & 1;
    b->sizex = SIZE[(flags >> 2) & 3];
    b->sizey = SIZE[(flags >> 4) & 3];

    int16_t sx = video.s2[offs + 1] + 0x100 * (video.s3[offs + 1] & 1) - SPRITE_X_OFFSET;
    int16_t sy = 256 - video.s2[offs] - 8 * b->sizey;
    sy = (sy & 0xFF) - 32;   // wraparound

    b->x = (WIDTH - 8) - sy;   // ROT90: column = 223 - MAME y, cell is 8 wide
    b->y = sx;
  }
}

// tilemap 36x28, no scroll. Pass TILES_ALL is opaque; TILES_PRIO redraws
// attr bit6 tiles over the sprites with lookup 15 transparent (cmap 0)
void phozon::blit_tile(short row, uint8_t col, TilePass pass) {
  uint16_t addr = tileaddr[row][col];
  uint8_t attr = memory[ATTR_OFF + addr];
  if (pass == TILES_PRIO && !(attr & 0x40)) {
    return;
  }

  const unsigned short *tile = tiles[memory[addr] + ((attr & 0x80) << 1)];
  const unsigned short *colors = (pass == TILES_PRIO) ? phozon_colormap_tiles_prio[attr & 0x3F]
                                                      : phozon_colormap_tiles[attr & 0x3F];

  unsigned short *ptr = frame_buffer + 8 * col;
  for (uint8_t r = 0; r < 8; r++, ptr += WIDTH) {
    unsigned short pix = tile[r];
    for (uint8_t c = 0; c < 8; c++, pix >>= 2) {
      unsigned short color = colors[pix & 3];
      if (color) {
        ptr[c] = color;
      }
    }
  }
}

// one 8x8 cell, clipped to this strip and to the 224 columns
void phozon::blit_cell(short row, uint16_t code, uint8_t color, short x, short y) {
  const unsigned short *cell = sprites[code];
  const unsigned short *colors = phozon_colormap_sprites[color];

  short top = 8 * row;
  short first = (y < top) ? top : y;
  short last = (y + 8 > top + 8) ? top + 8 : y + 8;
  uint8_t c0 = (x < 0) ? -x : 0;
  uint8_t c1 = (x > WIDTH - 8) ? WIDTH - x : 8;

  // ptr points at the first visible column, never before frame_buffer
  unsigned short *ptr = frame_buffer + WIDTH * (first - top) + x + c0;
  for (short line = first; line < last; line++, ptr += WIDTH) {
    unsigned short pix = cell[line - y] >> (2 * c0);
    for (uint8_t c = c0; c < c1; c++, pix >>= 2) {
      unsigned short col = colors[pix & 3];
      if (col) {
        ptr[c - c0] = col;
      }
    }
  }
}

// blocks in table order, later entries on top (MAME draw order)
void phozon::render_sprites(short row) {
  static const uint8_t GFX_OFFS[4][4] = {
    { 0, 1, 4, 5 }, { 2, 3, 6, 7 }, { 8, 9, 12, 13 }, { 10, 11, 14, 15 }
  };
  short top = 8 * row;

  for (uint8_t i = 0; i < n_blocks; i++) {
    const Block *b = &blocks[i];
    if (b->y >= top + 8 || b->y + 8 * (b->sizex + 1) <= top) {
      continue;
    }

    uint8_t variant = (b->flipy << 1) | b->flipx;
    for (uint8_t cx = 0; cx <= b->sizex; cx++) {
      short y = b->y + 8 * cx;
      if (y >= top + 8 || y + 8 <= top) {
        continue;
      }

      for (uint8_t cy = 0; cy <= b->sizey; cy++) {
        short x = b->x - 8 * cy;
        if (x >= WIDTH || x + 8 <= 0) {
          continue;
        }
        uint16_t code = (b->code + GFX_OFFS[cy ^ (b->sizey * b->flipy)][cx ^ (b->sizex * b->flipx)]) & 0x1FF;
        blit_cell(row, 4 * code + variant, b->color, x, y);
      }
    }
  }
}

void phozon::render_tiles(short row, TilePass pass) {
  for (uint8_t col = 0; col < 28; col++) {
    blit_tile(row, col, pass);
  }
}

void phozon::render_row(short row) {
  render_tiles(row, TILES_ALL);
  render_sprites(row);
  render_tiles(row, TILES_PRIO);
}

#ifdef LED_PIN
// from logos/phozon.png: navy/blue letters, green faces, red atoms
static const CRGB PHOZON_LED_BLUE = CRGB(0x193b58);
static const CRGB PHOZON_LED_GREEN = CRGB(0x8f9469);
static const CRGB PHOZON_LED_WHITE = CRGB(0xe3e5d9);
static const CRGB PHOZON_LED_RED = CRGB(0xa90000);

static const uint8_t WSG_VOICES = 8;
static const uint8_t LOUD_VOLUME = 12;   // 15XX volume 0..15

void phozon::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      PHOZON_LED_BLUE, PHOZON_LED_GREEN, PHOZON_LED_WHITE, PHOZON_LED_WHITE,
      PHOZON_LED_WHITE, PHOZON_LED_GREEN, PHOZON_LED_BLUE};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// menu colors, middle LEDs red while any 15XX voice plays loud
void phozon::gameLeds(CRGB *leds) {
  menuLeds(leds);
  if (!wsg_enable) {
    return;
  }

  for (uint8_t ch = 0; ch < WSG_VOICES; ch++) {
    if ((soundregs[ch * 8 + 3] & 0x0F) < LOUD_VOLUME) {
      continue;
    }
    for (uint8_t i = 2; i < 5; i++) {
      leds[i] = PHOZON_LED_RED;
    }
    return;
  }
}
#endif

#endif // ENABLE_PHOZON
