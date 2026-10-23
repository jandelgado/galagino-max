// ============================================================================
// SPINNERINO P4 - machines/motorace/motorace.cpp
//
// MotoRace USA (Irem 1983, Traverse USA / Zippy Race hardware)
// CPU: Z80 @ 3.072MHz main + M6803 @ 894KHz sound + 2x AY-3-8910
// Native raster 240x256 (x 8-247), shown ROT270: 256x240 landscape, road
// scrolling down. The panel shows screen x 16-255 (left road shoulder
// cropped, HUD kept), lines centred. Tile 8x8 3bpp, sprite 16x16, 64x32
// tilemap, native horizontal scroll.
// ============================================================================
#include "motorace.h"
#include "../../emulation/input.h"

#include "../../catalog.h"
#ifdef GAME_MOTORACE

#include "motorace_rom.h"
#include "motorace_snd_rom.h"
#include "motorace_tilemap.h"
#include "motorace_spritemap.h"
#include "motorace_cmap.h"

motorace::motorace()
  : motorace_rom(motorace_rom_blob),
    motorace_snd_rom(motorace_snd_rom_blob),
    motorace_spritemap(motorace_spritemap_blob),
    motorace_tilemap(motorace_tilemap_blob) {
  rom_ptr = motorace_rom.data();
  snd_rom_ptr = motorace_snd_rom.data();
}

// ── Z80 instruction fetch ──

unsigned char motorace::opZ80(unsigned short Addr) {
  if (Addr < 0x8000)
    return rom_ptr[Addr];
  return 0xFF;
}

// ── Z80 memory read ──
unsigned char motorace::rdZ80(unsigned short Addr) {
  if (Addr < 0x8000)
    return rom_ptr[Addr];

  if (Addr >= 0x8000 && Addr <= 0x8FFF)
    return memory[MR_MEM_VRAM + (Addr - 0x8000)];

  if (Addr >= 0xC800 && Addr <= 0xC9FF)
    return memory[MR_MEM_SPRITES + (Addr - 0xC800)];

  if (Addr == 0xD000) {
    unsigned char keymask = input->buttons_get();
    unsigned char val = 0xFF;
    if (keymask & BUTTON_START) val &= ~0x01;
    if (keymask & BUTTON_COIN)  val &= ~0x08;
    return val;
  }
  if (Addr == 0xD001) {
    unsigned char keymask = input->buttons_get();
    unsigned char val = 0xFF;
    if (keymask & BUTTON_RIGHT) val &= ~0x01;
    if (keymask & BUTTON_LEFT)  val &= ~0x02;
    if (keymask & BUTTON_FIRE)  val &= ~0x20;  // accelerate
    if (keymask & BUTTON_DOWN)  val &= ~0x80;  // brake
    return val;
  }
  if (Addr == 0xD002) return 0xFF;
  if (Addr == 0xD003) return MOTORACE_DSW1;
  if (Addr == 0xD004) return MOTORACE_DSW2;

  if (Addr >= 0xE000 && Addr <= 0xEFFF)
    return memory[MR_MEM_WORKRAM + (Addr - 0xE000)];

  return 0xFF;
}

// ── Z80 memory write ──
void motorace::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0x8000 && Addr <= 0x8FFF) {
    memory[MR_MEM_VRAM + (Addr - 0x8000)] = Value;
    return;
  }
  if (Addr == 0x9000) { scroll_x_low  = Value; return; }
  if (Addr == 0xA000) { scroll_x_high = Value; return; }

  if (Addr >= 0xC800 && Addr <= 0xC9FF) {
    memory[MR_MEM_SPRITES + (Addr - 0xC800)] = Value;
    return;
  }

  if (Addr == 0xD000) {
    sound_cmd = Value;
    if ((Value & 0x80) == 0)
      m6803_irq(&snd_cpu);
    return;
  }
  if (Addr == 0xD001) {
    flipscreen = Value & 1;
    if (!game_started && (Value & 0x02)) game_started = 1;
    return;
  }
  if (Addr >= 0xE000 && Addr <= 0xEFFF) {
    memory[MR_MEM_WORKRAM + (Addr - 0xE000)] = Value;
    return;
  }
}

// ============================================================================
// M6803 Sound CPU (Irem M52 sound board)
// ============================================================================
motorace *g_motorace_instance = nullptr;

static uint8_t motorace_snd_read(uint16_t addr)            { return g_motorace_instance->snd_read(addr); }
static void    motorace_snd_write(uint16_t addr, uint8_t v){ g_motorace_instance->snd_write(addr, v); }
static uint8_t motorace_snd_port_read(uint8_t port)        { return g_motorace_instance->snd_port_read(port); }
static void    motorace_snd_port_write(uint8_t port, uint8_t v){ g_motorace_instance->snd_port_write(port, v); }

void motorace::init(Input *inp, unsigned short *fb, sprite_S *sb, unsigned char *mb) {
  machineBase::init(inp, fb, sb, mb);
  g_motorace_instance = this;
}

void motorace::reset() {
  machineBase::reset();
  scroll_x_low = 0;
  scroll_x_high = 0;
  flipscreen = 0;
  boot_frames = 0;
  sound_cmd = 0;
  snd_port1 = 0;
  snd_port2 = 0;
  memset(ay_addr, 0, sizeof(ay_addr));
  memset(ay_regs, 0, sizeof(ay_regs));

  g_motorace_instance = this;
  m6803_ext_read_fn   = motorace_snd_read;
  m6803_ext_write_fn  = motorace_snd_write;
  m6803_port_read_fn  = motorace_snd_port_read;
  m6803_port_write_fn = motorace_snd_port_write;

  m6803_reset(&snd_cpu);
  snd_cpu.irq_pending = 1;  // MAME: IRQ asserted at reset
}

uint8_t motorace::snd_read(uint16_t addr) {
  if (addr == 0x0800) return sound_cmd;
  if (addr >= 0xC000)
    return snd_rom_ptr[addr - 0xC000];
  return 0xFF;
}

void motorace::snd_write(uint16_t addr, uint8_t val) {
  if (addr == 0x0800) {
    if ((sound_cmd & 0x80) != 0) snd_cpu.irq_pending = 0;
    return;
  }
  // ADPCM (MSM5205, non emulato)
}

uint8_t motorace::snd_port_read(uint8_t port) {
  if (port == 1) {
    if (snd_port2 & 0x08) {
      unsigned char reg = ay_addr[0] & 0x0F;
      if (reg == 14) return sound_cmd;
      return ay_regs[0][reg];
    }
    if (snd_port2 & 0x10)
      return ay_regs[1][ay_addr[1] & 0x0F];
    return 0xFF;
  }
  if (port == 2) return 0x00;
  return 0xFF;
}

void motorace::snd_port_write(uint8_t port, uint8_t val) {
  if (port == 1) { snd_port1 = val; return; }
  if (port == 2) {
    if ((snd_port2 & 0x01) && !(val & 0x01)) {
      if (snd_port2 & 0x04) {
        if (snd_port2 & 0x08) ay_addr[0] = snd_port1 & 0x0F;
        if (snd_port2 & 0x10) ay_addr[1] = snd_port1 & 0x0F;
      } else {
        if (snd_port2 & 0x08) {
          unsigned char reg = ay_addr[0] & 0x0F;
          ay_regs[0][reg] = snd_port1;
          if (reg < 14) soundregs[reg] = snd_port1;
        }
        if (snd_port2 & 0x10) {
          unsigned char reg = ay_addr[1] & 0x0F;
          ay_regs[1][reg] = snd_port1;
          if (reg < 14) soundregs[16 + reg] = snd_port1;
        }
      }
      m6803_internal_ram[0x3D] = 1;
    }
    snd_port2 = val;
    return;
  }
}

// ── Z80 + M6803 frame execution ──
void motorace::run_frame(void) {
  current_cpu = 0;
  for (int i = 0; i < INST_PER_FRAME; i++) {
    StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    if (i == INST_PER_FRAME - 1)
      IntZ80(&cpu[0], INT_IRQ);
  }

  // M6803 batch: ~14900 cycles per frame (894 KHz / 60 fps)
  static int nmi_cycle_cnt = 0;
  int snd_budget = 14900;
  while (snd_budget > 0) {
    int cyc = m6803_step(&snd_cpu);
    snd_budget -= cyc;
    nmi_cycle_cnt += cyc;
    if (nmi_cycle_cnt >= 224) {
      nmi_cycle_cnt -= 224;
      m6803_nmi(&snd_cpu);
    }
    uint16_t old_tc = snd_cpu.timer_counter;
    snd_cpu.timer_counter += cyc;
    if (snd_cpu.timer_counter < old_tc) snd_cpu.tcsr |= 0x20;
    if (old_tc <= snd_cpu.timer_output_compare &&
        snd_cpu.timer_counter >= snd_cpu.timer_output_compare)
      snd_cpu.tcsr |= 0x40;
  }

  // Attract mode never writes the game-start bit: stop the boot fast-forward
  // after the self test, else attract runs unthrottled.
  if (!game_started && ++boot_frames >= 120) game_started = 1;
}

// ── Sprite list in panel coordinates (ROT270: panel x = native y - 16,
// panel y = 256 - native x) ──
void motorace::prepare_frame(void) {
  active_sprites = 0;
  for (int offs = 0x1FF; offs >= 0; offs -= 4) {
    unsigned char *spr_base = &memory[MR_MEM_SPRITES + offs - 3];

    unsigned char sy_raw = spr_base[0];
    unsigned char attr   = spr_base[1];
    unsigned char code   = spr_base[2];
    unsigned char sx_raw = spr_base[3];

    if (sy_raw == 0 && sx_raw == 0) {
      continue;
    }

    int native_x = ((sx_raw + 8) & 0xFF) - 8;
    int native_y = 240 - sy_raw;

    struct sprite_S spr;
    spr.code  = code;
    spr.color = attr & 0x0F;
    spr.x = native_y - CROP_X;
    spr.y = TOP_LINE + VIS_X_MAX - SPRITE_SIZE + 1 - native_x;
    spr.flags = ((attr & 0x40) ? FLIP_ROWS : 0) | ((attr & 0x80) ? FLIP_COLS : 0);

    // sprites never cover the HUD (native y 192-255)
    if (spr.x <= -SPRITE_SIZE || spr.x >= SPRITE_CLIP_X) {
      continue;
    }
    sprite[active_sprites++] = spr;
    if (active_sprites >= 124) {
      break;
    }
  }
}

// ============================================================================
// Panel line = one native column (ROT270): native x = 247 - screen line,
// tilemap row = screen tile column. Tilemap rows 0-23 scroll, rows 24-31 are
// the fixed HUD. Tiles are stored rotated (romconv): row r of a stored tile
// is native column 7 - r. Colour 15 tiles (tunnels) keep pens 6, 7 in front
// of sprites, marked in front[].
// ============================================================================
void motorace::draw_tiles(short strip) {
  int scroll = scroll_x_low + (scroll_x_high << 8);
  memset(front, 0, sizeof(front));

  for (int k = 0; k < 8; k++) {
    unsigned short *line = frame_buffer + k * FB_W;
    int native_x = VIS_X_MAX - (strip * 8 + k);

    for (int col = CROP_X / 8; col < MAP_ROWS; col++) {
      int x0 = col * 8 - CROP_X;
      int map_x = (native_x + (col < SCROLL_ROWS ? scroll : 0)) & (MAP_W - 1);
      int tile_index = col * MAP_COLS + (map_x >> 3);
      unsigned char code_lo = memory[MR_MEM_VRAM + 2 * tile_index];
      unsigned char attr    = memory[MR_MEM_VRAM + 2 * tile_index + 1];

      int code = code_lo + ((attr & 0xC0) << 2);
      // TILE_FLIPXY: 0x20 flips native x, 0x10 native y
      int r = (attr & 0x20) ? (map_x & 7) : 7 - (map_x & 7);
      bool flip_cols = attr & 0x10;
      bool tunnel = (attr & 0x0F) == TUNNEL_COLOR;
      const unsigned short *colors = motorace_char_cmap[attr & 0x0F];
      uint32_t row = motorace_tilemap[code][r];

      for (int c = 0; c < 8; c++) {
        int sc = flip_cols ? 7 - c : c;
        unsigned char px = (row >> ((7 - sc) * 4)) & 7;
        line[x0 + c] = colors[px];
        if (tunnel && px >= TUNNEL_PEN) {
          front[x0 + c] |= 1 << k;
        }
      }
    }
  }
}

// Sprites are stored rotated too: row r is native column 15 - r.
void motorace::draw_sprite(short strip, unsigned char s) {
  int spr_x = sprite[s].x;
  int spr_y = sprite[s].y;
  bool flip_rows = sprite[s].flags & FLIP_ROWS;
  bool flip_cols = sprite[s].flags & FLIP_COLS;

  int strip_y = (strip + TOP_LINE / 8) * 8;
  int r_min = strip_y - spr_y;
  int r_max = strip_y + 7 - spr_y;
  if (r_min < 0) {
    r_min = 0;
  }
  if (r_max > SPRITE_SIZE - 1) {
    r_max = SPRITE_SIZE - 1;
  }

  const uint32_t *data = motorace_spritemap[sprite[s].code];
  const unsigned short *colors = motorace_spr_cmap[sprite[s].color];

  for (int r = r_min; r <= r_max; r++) {
    int k = spr_y + r - strip_y;
    int src_r = flip_rows ? (SPRITE_SIZE - 1 - r) : r;
    uint32_t lo = data[src_r * 2];
    uint32_t hi = data[src_r * 2 + 1];
    unsigned short *dst = frame_buffer + k * FB_W;

    for (int c = 0; c < SPRITE_SIZE; c++) {
      int x = spr_x + c;
      if (x < 0 || x >= SPRITE_CLIP_X || (front[x] & (1 << k))) {
        continue;
      }
      int sc = flip_cols ? (SPRITE_SIZE - 1 - c) : c;
      unsigned char px = (sc < 8) ? ((lo >> ((7 - sc) * 4)) & 7) : ((hi >> ((15 - sc) * 4)) & 7);
      if (px) {
        dst[x] = colors[px];
      }
    }
  }
}

// 240 screen lines centred on the 288 line panel, strips 3-32
void motorace::render_row(short row) {
  short strip = row - TOP_LINE / 8;
  if (strip < 0 || strip >= SCREEN_LINES / 8) {
    return;
  }

  draw_tiles(strip);

  int strip_y = row * 8;
  for (unsigned char s = 0; s < active_sprites; s++) {
    int spr_y = sprite[s].y;
    if (spr_y + SPRITE_SIZE - 1 >= strip_y && spr_y <= strip_y + 7) {
      draw_sprite(strip, s);
    }
  }
}

const Logo &motorace::logo(void) {
  return motorace_logo;
}

#endif // GAME_MOTORACE
