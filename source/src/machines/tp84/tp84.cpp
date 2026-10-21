#include "tp84.h"
#include "tp84_rom_main.h"
#include "tp84_rom_sub.h"
#include "tp84_rom_audio.h"
#include "tp84_tiles.h"
#include "tp84_sprites.h"
#include "tp84_palette.h"

tp84::tp84()
  : tp84_rom_main(tp84_rom_main_blob), tp84_rom_sub(tp84_rom_sub_blob),
    tp84_rom_audio(tp84_rom_audio_blob), tp84_tiles(tp84_tiles_blob),
    tp84_sprites(tp84_sprites_blob), tp84_rgb(tp84_rgb_blob), tp84_lut(tp84_lut_blob) {
  // m6809_reset() reads the reset vectors through these
  main_rom = tp84_rom_main.data();
  sub_rom = tp84_rom_sub.data();
  audio_rom = tp84_rom_audio.data();
  tiles = tp84_tiles.data();
  sprite_gfx = tp84_sprites.data();
  rgb = tp84_rgb.data();
  lut = tp84_lut.data();
}

void tp84::reset() {
  machineBase::reset();               // clears memory[], mutes SN chips, resets Z80
  bands.reset();
  line = 0;
  main_irq_enable = sub_irq_mask = 0;
  palette_bank = scroll_x = scroll_y = 0;
  soundlatch = snd_irq_pend = 0;
  snd_cycles = 0;
  main_polls = sub_polls = snd_polls = 0;
  cmap_bank = -1;
  led_flash = 0;

  m6809_reset(&main_cpu);
  main_cpu.rom_direct = main_rom;     // must follow m6809_reset()
  main_cpu.rom_base = 0x8000;
  main_cpu.rom_size = 0x8000;
  m6809_reset(&sub_cpu);
  sub_cpu.rom_direct = sub_rom;
  sub_cpu.rom_base = 0xE000;
  sub_cpu.rom_size = 0x2000;
}

void tp84::start() {
  reset();
  game_started = 1;
}

// Instruction fetches outside the ROM windows (RAM code)
unsigned char GAME_IRAM tp84::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

unsigned char GAME_IRAM tp84::m6809_read(m6809_state *s, uint16_t addr) {
  if (s == &sub_cpu) {
    if (addr >= 0xE000) return sub_rom[addr - 0xE000];
    if ((addr & 0xF800) == 0x8000) return memory[TP84_SHARED + (addr & 0x7FF)];
    if (addr >= 0x67A0 && addr < 0x6800) return bands.ram[addr - 0x67A0];
    if ((addr & 0xF800) == 0x6000) return memory[TP84_SUB_RAM + (addr & 0x7FF)];
    if (addr == 0x2000) {                     // beam position
      sub_polls++;
      return line;
    }
    return 0xFF;
  }
  if (addr >= 0x8000) return main_rom[addr - 0x8000];
  if (addr >= 0x4000 && addr < 0x5000) return memory[addr - 0x4000];
  if ((addr & 0xF800) == 0x5000) {
    if (addr == 0x523A) {                     // task queue pointer, idle loop only
      main_polls++;
    }
    return memory[TP84_SHARED + (addr & 0x7FF)];
  }
  return main_read_io(addr);
}

uint8_t tp84::main_read_io(uint16_t addr) {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xFF;
  switch (addr) {
    case 0x2800:                                         // SYSTEM
      if (keys & BUTTON_COIN)  v &= ~0x01;
      if (keys & BUTTON_START) v &= ~0x08;
      return v;
    case 0x2820:                                         // P1, 8-way
      if (keys & BUTTON_LEFT)  v &= ~0x01;
      if (keys & BUTTON_RIGHT) v &= ~0x02;
      if (keys & BUTTON_UP)    v &= ~0x04;
      if (keys & BUTTON_DOWN)  v &= ~0x08;
      if (keys & (BUTTON_FIRE | BUTTON_A | BUTTON_L1)) v &= ~0x10;   // shoot
      if (keys & (BUTTON_START | BUTTON_B | BUTTON_R1)) v &= ~0x20;  // missile
      return v;
    case 0x2860: return TP84_DSW1;
    case 0x3000: {
      uint8_t d = TP84_DSW2;
      if (input->demoSoundsOff()) d |= TP84_DSW2_DEMO_SOUND_OFF;
      return d;
    }
  }
  return 0xFF;                                           // P2 0x2840, unmapped
}

void GAME_IRAM tp84::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (s == &sub_cpu) {
    sub_polls = 0;
    if ((addr & 0xF800) == 0x8000) {
      memory[TP84_SHARED + (addr & 0x7FF)] = val;
      // conservative wake: sub never writes 0x5740-0x577F/0x523A, frame IRQ is the real wake
      main_polls = 0;
      return;
    }
    if (addr >= 0x67A0 && addr < 0x6800) { bands.write(addr - 0x67A0, val, line); return; }
    if ((addr & 0xF800) == 0x6000) { memory[TP84_SUB_RAM + (addr & 0x7FF)] = val; return; }
    if (addr == 0x4000) {
      sub_irq_mask = val & 1;
      if (!sub_irq_mask) sub_cpu.irq_pending = 0;
    }
    return;                                              // 0x0000 watchdog
  }
  main_polls = 0;
  if (addr >= 0x4000 && addr < 0x5000) { memory[addr - 0x4000] = val; return; }
  if ((addr & 0xF800) == 0x5000) { memory[TP84_SHARED + (addr & 0x7FF)] = val; return; }
  switch (addr) {
    case 0x2800: palette_bank = val; return;
    case 0x3000:                                         // LS259 Q0: irq enable
      main_irq_enable = val & 1;
      if (!main_irq_enable) main_cpu.irq_pending = 0;
      return;
    case 0x3800: snd_irq_pend = 1; return;
    case 0x3A00:
      soundlatch = val;
      if (val == TP84_SND_PLAYER_DIE) led_flash = TP84_LED_FLASH_FRAMES;
      return;
    case 0x3C00: scroll_x = val; return;
    case 0x3E00: scroll_y = val; return;
  }
  // 0x2000 watchdog, 0x3001-0x3007 coin counters / flip (upright): ignored
}

// ---- Sound Z80 ----

unsigned char tp84::opZ80(unsigned short Addr) { return rdZ80(Addr); }

unsigned char tp84::rdZ80(unsigned short Addr) {
  if (Addr < 0x2000) return audio_rom[Addr];
  if (Addr < 0x4000) return 0x00;                        // unpopulated ROM socket
  if ((Addr & 0xE000) == 0x4000) return memory[TP84_SND_RAM + (Addr & 0x3FF)];
  if ((Addr & 0xE000) == 0x6000) return soundlatch;
  if ((Addr & 0xE000) == 0x8000) {                      // (cycles / 1024) & 0x0F
    snd_polls++;
    return snd_cycles >> 10;
  }
  return 0xFF;
}

void tp84::wrZ80(unsigned short Addr, unsigned char Value) {
  snd_polls = 0;
  if ((Addr & 0xE000) == 0x4000) { memory[TP84_SND_RAM + (Addr & 0x3FF)] = Value; return; }
  switch (Addr) {
    case 0xC001: sn[0].write(Value); return;
    case 0xC003: sn[1].write(Value); return;
    case 0xC004: sn[2].write(Value); return;
  }
  // 0xA000-0xA1FF RC filters, 0xC000 nop
}

// ---- Frame ----

// One frame = 256 beam lines from vblank (line 240) to the next vblank.
// Per line: both 6809s ~100 cycles, Z80 20 instructions. The sub CPU polls
// the beam (0x2000) to multiplex sprites, so the beam advances per line.
//
// Poll loop skip (~half of all CPU time). Both 6809 poll loops reload
// their state from RAM each pass, so polls with no write by that CPU in
// between repeat until something external changes; the CPU then stops,
// its remaining cycles are dropped:
// - main task loop: wakes on the frame IRQ. A sub write to shared RAM also
//   wakes it (conservative: the sub never writes 0x5740-0x577F/0x523A).
// - sub beam loop (empty sprite queues): wakes at line 0xE0, its exit.
// The Z80 timer only changes between lines, so a second timer poll in a
// line spins to the line end; whole timer loops are dropped, the loop
// phase is kept (bit-exact).
void tp84::run_frame(void) {
  if (main_irq_enable) m6809_irq(&main_cpu);
  if (sub_irq_mask) m6809_irq(&sub_cpu);
  main_polls = sub_polls = 0;                            // let both CPUs take the IRQ
  bands.beginFrame();

  int main_debt = 0, sub_debt = 0;
  for (int n = 0; n < TP84_LINES; n++) {
    line = (uint8_t)(240 + n);                           // wraps 255 -> 0

    main_debt += TP84_CYCLES_LINE;
    while (main_debt > 0) {
      if (main_polls >= TP84_MAIN_SPIN) {
        main_debt = 0;
        break;
      }
      int c = m6809_step(&main_cpu, 4);
      main_debt -= (c > 0) ? c : 1;
    }
    sub_debt += TP84_CYCLES_LINE;
    while (sub_debt > 0) {
      if (sub_polls >= TP84_SUB_SPIN && line < TP84_SUB_SPIN_END) {
        sub_debt = 0;
        break;
      }
      int c = m6809_step(&sub_cpu, 4);
      sub_debt -= (c > 0) ? c : 1;
    }

    snd_polls = 0;
    for (int left = TP84_Z80_STEPS_LINE; left > 0; left--) {
      if (snd_polls >= TP84_SND_SPIN) {
        left %= TP84_SND_LOOP;
        if (!left) {
          break;
        }
      }
      StepZ80(&cpu[0]);
    }
    snd_cycles = (snd_cycles + TP84_Z80_CYCLES_LINE) & 0x3FFF;
    if (snd_irq_pend && (cpu[0].IFF & IFF_1)) {
      IntZ80(&cpu[0], INT_RST38);
      snd_irq_pend = 0;
    }
  }
  line = 240;

  publish_video();
}

// ---- Video ----

// Latch everything render reads at line 240, where MAME draws the frame
// when no sprite write forced a partial update.
void tp84::publish_video() {
  vblank.publish([this](VideoState &v) {
    static const uint8_t cols[4] = {0, 1, 30, 31};
    memcpy(v.bg_vram, &memory[TP84_BG_VRAM], 0x400);
    memcpy(v.bg_cram, &memory[TP84_BG_CRAM], 0x400);
    for (int ty = 0; ty < 32; ty++) {
      for (int i = 0; i < 4; i++) {
        v.hud_vram[ty][i] = memory[TP84_FG_VRAM + ty * 32 + cols[i]];
        v.hud_cram[ty][i] = memory[TP84_FG_CRAM + ty * 32 + cols[i]];
      }
    }
    v.scroll_x = scroll_x;
    v.scroll_y = scroll_y;
    v.palette_bank = palette_bank;
    v.n_sprites = bands.finish(v.sprites);
  });
}

// Char pen: rgb[0x80 | j<<4 | lut[(b & 0x18)<<3 | c<<2 | p]],
// sprite pen: rgb[j<<4 | lut[0x100 | c<<4 | p]], nibble 0 transparent.
void tp84::build_cmaps(uint8_t bank) {
  const uint8_t j = bank & 7;
  const uint16_t cbase = (bank & 0x18) << 3;
  for (int c = 0; c < 16; c++)
    for (int p = 0; p < 4; p++)
      cmap_tiles[c][p] = rgb[0x80 | (j << 4) | (lut[cbase | (c << 2) | p] & 0x0F)];
  for (int c = 0; c < 16; c++) {
    sprite_opaque[c] = 0;
    for (int p = 0; p < 16; p++) {
      const uint8_t n = lut[0x100 | (c << 4) | p] & 0x0F;
      cmap_sprites[c][p] = rgb[(j << 4) | n];
      if (n) sprite_opaque[c] |= 1 << p;
    }
  }
  cmap_bank = bank;
}

void tp84::prepare_frame(void) {
  vblank.read(video);
  if (video.palette_bank != cmap_bank) build_cmaps(video.palette_bank);
}

// One galagino line of a tilemap. idx = ty * stride + col: bg passes the
// full 32x32 map (stride 32, col = tile x), the HUD its 4 saved columns
// (stride 4). xin = x inside the tile, tm_y0 = tilemap y of column 0
// (native Y 239). Columns walk native Y down.
static inline void GAME_IRAM tile_line(uint16_t *dst, const uint8_t *vram, const uint8_t *cram,
    uint8_t stride, uint8_t col, const uint16_t (*tiles)[8], const uint16_t (*cmap)[4],
    uint8_t xin, uint8_t tm_y0) {
  uint8_t ty = tm_y0 >> 3;
  int k0 = 7 - (tm_y0 & 7);
  int c = 0;
  while (c < 224) {
    const uint16_t idx = ty * stride + col;
    const uint8_t attr = cram[idx];
    const uint16_t code = ((attr & 0x30) << 4) | vram[idx];
    const uint16_t bits = tiles[code][(attr & 0x40) ? 7 - xin : xin];  // bit6 flipX: native x
    const uint16_t *pal = cmap[attr & 0x0F];
    const bool flip_y = attr & 0x80;                                    // bit7 flipY: native y
    for (int k = k0; k < 8 && c < 224; k++, c++) {
      const int kk = flip_y ? 7 - k : k;
      dst[c] = pal[(bits >> (2 * kk)) & 3];
    }
    k0 = 0;
    ty = (ty - 1) & 31;
  }
}

// Strip row 2..33 = native X 8*(row-2) .. +7; column c = native Y 239 - c.
void GAME_IRAM tp84::render_row(short row) {
  if (row < 2 || row >= 34 || !game_started) return;   // frame_buffer arrives cleared
  const bool hud = row < 4 || row >= 32;               // native X 0-15 / 240-255
  for (int l = 0; l < 8; l++) {
    const uint8_t nx = 8 * (row - 2) + l;
    uint16_t *dst = frame_buffer + l * 224;
    if (hud) {                                         // fg, not scrolled, opaque
      const uint8_t tx = nx >> 3;                      // 0, 1, 30 or 31
      tile_line(dst, &video.hud_vram[0][0], &video.hud_cram[0][0], 4,
                tx < 2 ? tx : tx - 28, tiles, cmap_tiles, nx & 7, 239);
    } else {
      const uint8_t tm_x = nx + video.scroll_x;
      tile_line(dst, video.bg_vram, video.bg_cram, 32, tm_x >> 3, tiles, cmap_tiles,
                tm_x & 7, (uint8_t)(239 + video.scroll_y));
    }
  }
  if (hud) return;                                     // fg covers sprites there
  for (uint8_t i = 0; i < video.n_sprites; i++) blit_sprite(row, video.sprites[i]);
}

// Native box X [x, x+16), Y [y, y+16), clipped to the slot's band
// [top, end) and visible Y 16..239. No wrap (MAME transmask clips).
void GAME_IRAM tp84::blit_sprite(short row, const tp84_hw::Sprite &s) {
  const int x = s.x, y = 240 - (int)s.sy;
  const int r0 = 8 * row - 16, r1 = r0 + 8;            // native X range of this strip
  const int xa = x > r0 ? x : r0, xb = (x + 16) < r1 ? (x + 16) : r1;
  if (xa >= xb) return;
  int ya = y, yb = y + 16;
  if (ya < s.top) ya = s.top;
  if (yb > s.end) yb = s.end;
  if (ya < 16) ya = 16;
  if (yb > 240) yb = 240;
  if (ya >= yb) return;

  const uint8_t (*gfx)[8] = sprite_gfx[s.code];
  const uint8_t color = s.attr & 0x0F;
  const uint16_t *pal = cmap_sprites[color];
  const uint16_t opaque = sprite_opaque[color];
  const bool flip_x = !(s.attr & 0x40), flip_y = s.attr & 0x80;

  for (int X = xa; X < xb; X++) {
    const int i = flip_x ? 15 - (X - x) : X - x;
    uint16_t *dst = frame_buffer + (X - r0) * 224;
    for (int Y = ya; Y < yb; Y++) {
      const int j = flip_y ? 15 - (Y - y) : Y - y;
      const uint8_t p = (gfx[j][i >> 1] >> ((i & 1) << 2)) & 0x0F;
      if (opaque & (1 << p)) dst[239 - Y] = pal[p];
    }
  }
}

#ifdef LED_PIN
// logo colors: navy with a silver center, red while the player dies
void tp84::gameLeds(CRGB *leds) {
  if (led_flash) {
    led_flash--;
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(0xaf0000);
    return;
  }
  for (int i = 0; i < NUM_LEDS; i++)
    leds[i] = (i == NUM_LEDS / 2) ? CRGB(0xbdbdbd) : CRGB(0x0e0050);
}

void tp84::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[7] = {CRGB(0xaf0000), CRGB(0xd2d200), CRGB(0xbdbdbd),
      CRGB(0xbdbdbd), CRGB(0xbdbdbd), CRGB(0xd2d200), CRGB(0xaf0000)};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
