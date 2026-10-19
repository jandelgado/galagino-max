#include "digdug2.h"
#include "../../emulation/spriteflip.h"

namespace {

// 56XX/58XX input bits, active low
constexpr unsigned char IN_UP = 0x01, IN_RIGHT = 0x02, IN_DOWN = 0x04, IN_LEFT = 0x08;
constexpr unsigned char IN_COIN1 = 0x01;
constexpr unsigned char IN_BUTTON1 = 0x01, IN_START1 = 0x04;
constexpr unsigned char IN_BUTTON2 = 0x01;   // DSW0 port

// START button: BUTTON_START with a COIN pin (EXTRA is then COIN) or the
// galagino controller, else START itself reports as BUTTON_EXTRA
#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
constexpr unsigned int START_KEY = BUTTON_START;
#else
constexpr unsigned int START_KEY = BUTTON_EXTRA;
#endif

constexpr unsigned int DRILL_KEYS = BUTTON_B | BUTTON_Y | BUTTON_R1 | START_KEY;

constexpr uint16_t SPRITE_TAB1 = 0x1780;
constexpr uint16_t SPRITE_TAB2 = 0x1F80;
constexpr uint16_t SPRITE_TAB3 = 0x2780;

constexpr unsigned char SPRITE_COLOR_MASK = 0x0F;   // 16 groups (todruaga 64)

} // namespace

digdug2::digdug2()
  : digdug2(Board{digdug2_colormap_sprites_blob, digdug2_rom_main_blob, digdug2_rom_sub_blob,
                  digdug2_sprites_blob, digdug2_tilemap_blob, digdug2_colormap_tiles,
                  digdug2_colormap_tiles_prio, digdug2_wavetable, IO_58XX}) { }

digdug2::digdug2(const Board &b)
  : digdug2_colormap_sprites(b.colormapSprites),
    digdug2_rom_main(b.romMain),
    digdug2_rom_sub(b.romSub),
    digdug2_sprites(b.sprites),
    digdug2_tilemap(b.tilemap),
    rom_main(digdug2_rom_main.data()), rom_sub(digdug2_rom_sub.data()),
    tiles(digdug2_tilemap.data()), cmap_tiles(b.cmapTiles),
    cmap_prio(b.cmapPrio),
    cmap_sprites(digdug2_colormap_sprites.data()),
    wavetable(b.wavetable), chip0(b.chip0) { }

// ============================================================================
// Reset
// ============================================================================

void digdug2::reset() {
  machineBase::reset();

  main_irq_mask = sub_irq_mask = 0;
  wsg_enable = 0;
  sub_reset = 1;          // mainlatch Q5=0 at power-on: sub CPU held in reset
  io_reset = 1;           // mainlatch Q4=0: namcoio held in reset
  scroll = 0;
  dipmux_sel = 0;
  drill_active = 0;

  io_chips_reset();

  m6809_reset(&main_cpu);
  m6809_reset(&sub_cpu);
  install_rom_direct();
}

// opcode fetch straight from ROM; m6809_reset() clears the window, so it is
// reinstalled after every reset
void digdug2::install_rom_direct(void) {
  main_cpu.rom_direct = rom_main;
  main_cpu.rom_base = 0x8000;
  main_cpu.rom_size = 0x8000;
  sub_cpu.rom_direct = rom_sub;
  sub_cpu.rom_base = 0xE000;
  sub_cpu.rom_size = 0x2000;
}

void digdug2::io_chips_reset(void) {
  io[0].reset();
  io[1].reset();
}

// ============================================================================
// Mainlatch LS259 (main 0x5000-0x500F, sub 0x2000-0x200F, A0 = data)
// ============================================================================

void digdug2::mainlatch_w(uint16_t addr) {
  unsigned char v = addr & 1;

  switch ((addr >> 1) & 7) {
    case 0:   // sub CPU irq mask; mask->0 = acknowledge
      sub_irq_mask = v;
      if (!v) {
        sub_cpu.irq_pending = 0;
      }
      break;
    case 1:   // main CPU irq mask
      main_irq_mask = v;
      if (!v) {
        main_cpu.irq_pending = 0;
      }
      break;
    case 2:   // flip screen: ignored (upright only)
      break;
    case 3:   // WSG 15XX sound enable
      wsg_enable = v;
      break;
    case 4:   // !reset of both namcoio
      if (!v && !io_reset) {
        io_chips_reset();
      }
      io_reset = !v;
      break;
    case 5:   // !reset of the sub CPU
      if (v && sub_reset) {
        m6809_reset(&sub_cpu);
        install_rom_direct();
      }
      sub_reset = !v;
      break;
  }
}

// ============================================================================
// M6809 bus, dispatched by CPU state pointer (main vs sub)
// ============================================================================

unsigned char IRAM_ATTR digdug2::m6809_read(m6809_state *s, uint16_t addr) {
  if (s == &sub_cpu) {
    // sub: 0x0000-0x03FF shared RAM (0x00-0x3F = WSG registers)
    if (addr < 0x0400) {
      return (addr < 0x40) ? soundregs[addr] : memory[DIGDUG2_SHARED_OFF + addr];
    }
    if (addr >= 0xE000) {
      return rom_sub[addr - 0xE000];
    }
    return 0xFF;
  }

  if (addr < 0x2800) {                    // VRAM + work/sprite RAM
    return memory[addr];
  }

  if ((addr & 0xFC00) == 0x4000) {        // RAM shared with sub
    uint16_t o = addr & 0x03FF;
    return (o < 0x40) ? soundregs[o] : memory[DIGDUG2_SHARED_OFF + o];
  }

  if ((addr & 0xFFE0) == 0x4800) {        // namcoio 0/1: 4 bit, high bits 1
    return io[(addr >> 4) & 1].read(addr);
  }

  if (addr >= 0x8000) {
    return rom_main[addr - 0x8000];
  }

  return 0xFF;
}

void IRAM_ATTR digdug2::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (s == &sub_cpu) {
    if (addr < 0x0400) {
      if (addr < 0x40) {
        soundregs[addr] = val;
      } else {
        memory[DIGDUG2_SHARED_OFF + addr] = val;
      }
      return;
    }
    if ((addr & 0xFFF0) == 0x2000) {
      mainlatch_w(addr);
    }
    return;
  }

  if (addr < 0x2800) {
    memory[addr] = val;
    // first character written to VRAM = boot done (60 Hz sync, attract)
    if (!game_started && addr < 0x0800 && val != 0) {
      game_started = 1;
    }
    return;
  }

  if ((addr & 0xF800) == 0x3800) {        // scroll_w: value = offset >> 3
    scroll = (addr >> 3) & 0xFF;
    return;
  }

  if ((addr & 0xFC00) == 0x4000) {
    uint16_t o = addr & 0x03FF;
    if (o < 0x40) {
      soundregs[o] = val;
    } else {
      memory[DIGDUG2_SHARED_OFF + o] = val;
    }
    return;
  }

  if ((addr & 0xFFE0) == 0x4800) {        // namcoio: 4 bit RAM
    io[(addr >> 4) & 1].write(addr, val);
    return;
  }

  if ((addr & 0xFFF0) == 0x5000) {
    mainlatch_w(addr);
    return;
  }

  // 0x8000 = watchdog: unused by digdug2 (MAME nops it)
}

unsigned char IRAM_ATTR digdug2::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  if (s == &sub_cpu) {
    if (addr >= 0xE000) {
      return rom_sub[addr - 0xE000];
    }
  } else if (addr >= 0x8000) {
    return rom_main[addr - 0x8000];
  }
  return m6809_read(s, addr);
}

// ============================================================================
// Frame loop: fine main/sub interleave + vblank
// ============================================================================

void digdug2::run_frame(void) {
  for (int i = 0; i < DIGDUG2_SLICES; i++) {
    m6809_step(&main_cpu, 4);
    if (!sub_reset) {
      m6809_step(&sub_cpu, 4);
    }
  }

  // latch render input before the vblank IRQ handler rewrites it
  vblank.publish([this](VideoState &v) {
    memcpy(v.tab1, &memory[SPRITE_TAB1], sizeof(v.tab1));
    memcpy(v.tab2, &memory[SPRITE_TAB2], sizeof(v.tab2));
    memcpy(v.tab3, &memory[SPRITE_TAB3], sizeof(v.tab3));
    v.scroll = scroll;
  });

  // vblank: IRQs if enabled by the mainlatch, then the namcoio run the
  // command left in their RAM (MAME: 50 us after vblank)
  if (main_irq_mask) {
    m6809_irq(&main_cpu);
  }

  if (sub_irq_mask && !sub_reset) {
    m6809_irq(&sub_cpu);
  }

  if (!io_reset) {
    if (chip0 == IO_56XX) {
      io[0].run56([this](uint8_t p) { return io_in(0, p); }, [](uint8_t) {});
    } else {
      io[0].run58([this](uint8_t p) { return io_in(0, p); }, [](uint8_t) {});
    }
    io[1].run56([this](uint8_t p) { return io_in(1, p); },
                [this](uint8_t v) { dipmux_sel = v; });
  }
}

// ============================================================================
// Namco custom I/O (MAME namcoio.cpp). 4 bit ports, active low.
// chip 0: 0=COINS 1=P1 2=P2 3=BUTTONS; chip 1: 0=dipmux(DSW2) 1/2=DSW1 3=DSW0
// ============================================================================

// BUTTON1 pumps, BUTTON2 drills. GPIO set has one action button, so START
// doubles as the drill (as system1 Regulus bomb); the controller adds B/Y/R1.
bool digdug2::drill_pressed(unsigned int keys) const {
  return (keys & DRILL_KEYS) != 0;
}

unsigned char digdug2::io_in(unsigned char chip, unsigned char port) {
  unsigned int keys = input->buttons_get();
  unsigned char v = 0x0F;

  if (chip == 0) {
    switch (port) {
      case 0:   // COINS: bit0 coin1, bit1 coin2, bit3 service
        if (keys & BUTTON_COIN) {
          v &= ~IN_COIN1;
        }
        break;
      case 1:   // P1 4-way joystick
        if (keys & BUTTON_UP) {
          v &= ~IN_UP;
        }
        if (keys & BUTTON_RIGHT) {
          v &= ~IN_RIGHT;
        }
        if (keys & BUTTON_DOWN) {
          v &= ~IN_DOWN;
        }
        if (keys & BUTTON_LEFT) {
          v &= ~IN_LEFT;
        }
        break;
      case 2:   // P2 (cocktail): not connected
        break;
      case 3:   // BUTTONS: bit0 button1 (pump), bit2 start1, bit3 start2
        if (keys & BUTTON_FIRE) {
          v &= ~IN_BUTTON1;
        }
        if (keys & BUTTON_START) {
          v &= ~IN_START1;
        }
        break;
    }
    return v;
  }

  switch (port) {
    case 0: return (dipmux_sel ? (DIGDUG2_DSW2 >> 4) : DIGDUG2_DSW2) & 0x0F;
    case 1: return DIGDUG2_DSW1 & 0x0F;
    case 2: return (DIGDUG2_DSW1 >> 4) & 0x0F;
    case 3:
      v = DIGDUG2_DSW0 & 0x0F;
      drill_active = drill_pressed(keys);
      if (drill_active) {
        v &= ~IN_BUTTON2;
      }
      return v;
  }
  return 0x0F;
}

// ============================================================================
// Video: sprites
// MAME coordinates (X horizontal 0..287, Y vertical 0..223) -> galagino:
//   gal_y = mame_x, gal_x = 223 - mame_y -> 16x16 top at 208 - sy
// Converter flip variants: index = (mame_flipy << 1) | mame_flipx = tab3 & 3
// ============================================================================

void digdug2::prepare_frame(void) {
  static const unsigned char gfx_offs[2][2] = { { 0, 1 }, { 2, 3 } };

  vblank.read(video);

  active_sprites = 0;
  for (int offs = 0; offs < SPRITE_TAB_SIZE && active_sprites < 124; offs += 2) {
    if (video.tab3[offs + 1] & 2) {   // bit1 = disable
      continue;
    }

    unsigned char code  = video.tab1[offs];
    unsigned char color = video.tab1[offs + 1] & SPRITE_COLOR_MASK;
    unsigned char flags = video.tab3[offs];
    unsigned char flipx = flags & 1;
    unsigned char flipy = (flags >> 1) & 1;
    unsigned char sizex = (flags >> 2) & 1;   // doubles along mame X = gal Y
    unsigned char sizey = (flags >> 3) & 1;   // doubles along mame Y = gal X

    int sx = video.tab2[offs + 1] + 0x100 * (video.tab3[offs + 1] & 1) - 40;
    int sy = 256 - video.tab2[offs] + 1;

    code &= ~sizex;
    code &= ~(sizey << 1);
    sy -= 16 * sizey;
    sy = (sy & 0xFF) - 32;

    // each 16x16 cell of the (up to 2x2) block becomes one galagino sprite
    for (unsigned char y = 0; y <= sizey; y++) {
      for (unsigned char x = 0; x <= sizex; x++) {
        short gal_x = 208 - (sy + 16 * y);
        short gal_y = sx + 16 * x;
        if (gal_x <= -16 || gal_x >= 224 || gal_y <= -16 || gal_y >= 288) {
          continue;
        }

        sprite_S *sp = &sprite[active_sprites++];
        sp->code  = (unsigned char)(code + gfx_offs[y ^ (sizey & flipy)][x ^ (sizex & flipx)]);
        sp->color = color;
        sp->x     = gal_x;
        sp->y     = gal_y;
        sp->flags = flags & 3;
      }
    }
  }
}

// 16x16 4bpp: 2 words per galagino row (nibbles LSB-first),
// transparent = colormap 0 (lookup 0xF in the converter)
void digdug2::blit_sprite(short row, unsigned char s) {
  SpriteRows4bpp spr(digdug2_sprites[sprite[s].code], sprite[s].flags & 3);
  const unsigned short *colors = cmap_sprites[sprite[s].color & SPRITE_COLOR_MASK];

  short y_offset = sprite[s].y - 8 * row;

  unsigned char lines2draw = 8;
  if (y_offset < -8) {
    lines2draw = 16 + y_offset;
  }

  unsigned short startline = 0;
  if (y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  if (y_offset < 0) {
    spr.skip(-y_offset);   // rows already drawn in the strip above
  }

  short x = sprite[s].x;
  unsigned char c0 = (x < 0) ? -x : 0;
  unsigned char c1 = (x > 224 - 16) ? 224 - x : 16;

  unsigned short *ptr = frame_buffer + x + 224 * startline;
  for (unsigned char r = 0; r < lines2draw; r++, ptr += 224) {
    uint32_t pix[2];
    spr.next(pix);
    for (unsigned char c = c0; c < c1; c++) {
      unsigned short col = colors[(pix[c >> 3] >> (4 * (c & 7))) & 0x0F];
      if (col) {
        ptr[c] = col;
      }
    }
  }
}

// ============================================================================
// Video: tilemap 36 columns x 60 rows (MAME mappy_tilemap_scan)
// ============================================================================

void digdug2::blit_tile(unsigned short idx, short x, char prio) {
  unsigned char attr = memory[0x800 + idx];
  if (prio && !(attr & 0x40)) {
    return;
  }

  const unsigned short *tile = tiles[memory[idx]];
  const unsigned short *colors = prio ? cmap_prio[attr & 0x3F]
                                      : cmap_tiles[attr & 0x3F];

  unsigned char c0 = (x < 0) ? -x : 0;
  unsigned char c1 = (x > 224 - 8) ? 224 - x : 8;

  unsigned short *ptr = frame_buffer + x;
  for (unsigned char r = 0; r < 8; r++, ptr += 224) {
    unsigned short pix = tile[r] >> (2 * c0);
    for (unsigned char c = c0; c < c1; c++, pix >>= 2) {
      unsigned short col = colors[pix & 3];
      if (!prio || col) {
        ptr[c] = col;   // first pass opaque, priority pass skips transparent
      }
    }
  }
}

void digdug2::render_tiles(short row, char prio) {
  int colp = row - 2;   // mappy_tilemap_scan "col -= 2"

  if (row < 2 || row > 33) {
    // fixed columns: special indexing at 0x780-0x7FF, no scroll
    for (unsigned char rt = 0; rt < 28; rt++) {
      unsigned short idx = ((rt + 2) & 0x0F) + (rt & 0x10) + ((colp & 3) << 5) + 0x780;
      blit_tile(idx, 216 - 8 * rt, prio);
    }
    return;
  }

  // scrolling 32x60 part: hw row rt at gal_x = 216 - 8*rt + scroll (mod 480)
  unsigned char rt0 = video.scroll >> 3;
  short fine = video.scroll & 7;
  for (unsigned char k = 0; k < 29; k++) {
    unsigned char rt = rt0 + k;
    if (rt >= 60) {
      rt -= 60;
    }
    short x = 216 - 8 * k + fine;
    if (x <= -8 || x >= 224) {
      continue;
    }
    blit_tile((rt << 5) + colp, x, prio);
  }
}

void digdug2::render_row(short row) {
  render_tiles(row, 0);

  for (unsigned char s = 0; s < active_sprites; s++) {
    if ((sprite[s].y < 8 * (row + 1)) && ((sprite[s].y + 16) > 8 * row)) {
      blit_sprite(row, s);
    }
  }

  render_tiles(row, 1);
}

// ============================================================================
// LEDs: colors from logos/digdug2.png
// ============================================================================

#ifdef LED_PIN
namespace {
const CRGB LED_DD2_BLUE  = CRGB(0x242799);
const CRGB LED_DD2_RED   = CRGB(0xcb264a);
const CRGB LED_DD2_WHITE = CRGB(0xdcd4e3);
} // namespace

void digdug2::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[7] = {
    LED_DD2_BLUE, LED_DD2_RED, LED_DD2_WHITE, LED_DD2_WHITE,
    LED_DD2_WHITE, LED_DD2_RED, LED_DD2_BLUE
  };
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// drill (jackhammer) lights the strip white, else the menu gradient
void digdug2::gameLeds(CRGB *leds) {
  if (!drill_active) {
    menuLeds(leds);
    return;
  }
  for (unsigned char c = 0; c < NUM_LEDS; c++) {
    leds[c] = LED_DD2_WHITE;
  }
}
#endif
