// ============================================================================
// machines/mpatrol/mpatrol.cpp
//
// Moon Patrol (Irem 1982, M52 hardware), MAME irem/m52.cpp.
// CPU: Z80 @ 3.072 MHz main + M6803 @ 894 kHz sound + 2x AY-3-8910.
// ROT0, visible 240x252: native column == frame_buffer column, native line l
// goes to galagino row l + FIRST_ROW. Layers: 3 parallax bitmaps (mountains,
// hills, city), 32x32 char layer (bottom quarter scrolls), 16x16 sprites.
// ============================================================================
#include "mpatrol.h"
#include "../../emulation/input.h"

#ifdef ENABLE_MPATROL

#include "mpatrol_rom.h"
#include "mpatrol_snd_rom.h"
#include "mpatrol_tilemap.h"
#include "mpatrol_spritemap.h"
#include "mpatrol_bg.h"
#include "mpatrol_cmap.h"

using namespace mpatrol_video;

// sprite_S::flags, = sprite RAM attr b6/b7 shifted down
static const uint8_t SPR_FLIPX = 0x01;
static const uint8_t SPR_FLIPY = 0x02;

mpatrol::mpatrol()
  : mpatrol_rom(mpatrol_rom_blob),
    mpatrol_snd_rom(mpatrol_snd_rom_blob),
    mpatrol_tilemap(mpatrol_tilemap_blob),
    mpatrol_spritemap(mpatrol_spritemap_blob),
    mpatrol_bg(mpatrol_bg_blob) {
  rom_ptr = mpatrol_rom.data();
  snd_rom_ptr = mpatrol_snd_rom.data();
  tiles = mpatrol_tilemap.data();
  sprites = mpatrol_spritemap.data();
  bgs = mpatrol_bg.data();
}

// ── Z80 ──

unsigned char mpatrol::opZ80(unsigned short Addr) {
  if (Addr < 0x4000) { return rom_ptr[Addr]; }
  return rdZ80(Addr);
}

unsigned char mpatrol::rdZ80(unsigned short Addr) {
  if (Addr < 0x4000) { return rom_ptr[Addr]; }
  if (Addr >= 0x8000 && Addr < 0x8800) { return memory[MP_MEM_VRAM + (Addr - 0x8000)]; }
  if (Addr >= 0x8800 && Addr < 0x9000) { return protection(bg1x); }
  if (Addr >= 0xd000 && Addr < 0xd800) {
    uint8_t keys = input->buttons_get();
    uint8_t v = 0xff;
    switch (Addr & 7) {
      case 0:  // IN0
        if (keys & BUTTON_START) { v &= ~0x01; }
        if (keys & BUTTON_COIN) { v &= ~0x08; }
        return v;
      case 1:  // IN1
        if (keys & BUTTON_RIGHT) { v &= ~0x01; }
        if (keys & BUTTON_LEFT) { v &= ~0x02; }
        if (keys & BUTTON_UP) { v &= ~0x20; }  // jump (BUTTON2), no 2nd button on GPIO
        if (keys & BUTTON_FIRE) { v &= ~0x80; }  // fire (BUTTON1)
        return v;
      case 3: return MPATROL_DSW1;
      case 4: return MPATROL_DSW2;
      default: return 0xff;  // IN2 (cocktail), unmapped
    }
  }
  if (Addr >= 0xe000 && Addr < 0xe800) { return memory[MP_MEM_WORK + (Addr - 0xe000)]; }
  return 0xff;
}

void mpatrol::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0x8000 && Addr < 0x8800) {
    memory[MP_MEM_VRAM + (Addr - 0x8000)] = Value;
    return;
  }
  if (Addr >= 0xc800 && Addr < 0xd000) {
    memory[MP_MEM_SPRITES + (Addr & 0x3ff)] = Value;
    return;
  }
  if (Addr >= 0xd000 && Addr < 0xd800) {
    if ((Addr & 3) == 0) {
      sound_cmd = Value;
      if (!(Value & 0x80)) { m6803_irq(&snd_cpu); }
    } else if ((Addr & 3) == 1) {
      // flip screen + coin counters; flip ignored (upright cabinet)
      game_started = 1;
    }
    return;
  }
  if (Addr >= 0xe000 && Addr < 0xe800) { memory[MP_MEM_WORK + (Addr - 0xe000)] = Value; }
}

void mpatrol::outZ80(unsigned short Port, unsigned char Value) {
  switch (Port & 0xe0) {
    case 0x00: scroll = Value; break;
    case 0x40: bg1x = Value; break;
    case 0x60: bg1y = Value; break;
    case 0x80: bg2x = Value; break;
    case 0xa0: bg2y = Value; break;
    case 0xc0: bgctl = Value; break;
  }
}

// ── M6803 sound CPU (Irem M52 small sound map) ──

static mpatrol *g_mpatrol_instance = nullptr;

static uint8_t mpatrol_snd_read(uint16_t addr)              { return g_mpatrol_instance->snd_read(addr); }
static void    mpatrol_snd_write(uint16_t addr, uint8_t v)  { g_mpatrol_instance->snd_write(addr, v); }
static uint8_t mpatrol_snd_port_read(uint8_t port)          { return g_mpatrol_instance->snd_port_read(port); }
static void    mpatrol_snd_port_write(uint8_t port, uint8_t v) { g_mpatrol_instance->snd_port_write(port, v); }

void mpatrol::init(Input *inp, unsigned short *fb, sprite_S *sb, unsigned char *mb) {
  machineBase::init(inp, fb, sb, mb);
  g_mpatrol_instance = this;
}

void mpatrol::reset() {
  machineBase::reset();
  scroll = bg1x = bg1y = bg2x = bg2y = bgctl = 0;
  sound_cmd = 0;
  snd_port1 = 0;
  snd_port2 = 0;
  nmi_cycle_cnt = 0;
  memset(ay_addr, 0, sizeof(ay_addr));
  memset(ay_regs, 0, sizeof(ay_regs));

  g_mpatrol_instance = this;
  m6803_ext_read_fn   = mpatrol_snd_read;
  m6803_ext_write_fn  = mpatrol_snd_write;
  m6803_port_read_fn  = mpatrol_snd_port_read;
  m6803_port_write_fn = mpatrol_snd_port_write;

  m6803_reset(&snd_cpu);
  snd_cpu.irq_pending = 1;  // MAME: IRQ asserted at reset (latch 0)
}

// global_mask 0x7fff: ROM mp-s1 at 0x7000-0x7fff (vectors at 0xfff0 read 0x7ff0)
uint8_t mpatrol::snd_read(uint16_t addr) {
  addr &= 0x7fff;
  if (addr >= 0x7000) { return snd_rom_ptr[addr - 0x7000]; }
  return 0xff;
}

void mpatrol::snd_write(uint16_t addr, uint8_t val) {
  addr &= 0x7fff;
  if (addr >= 0x1000 && addr < 0x2000) {  // IRQ ack
    if (sound_cmd & 0x80) { snd_cpu.irq_pending = 0; }
  }
  // 0x0000-0x0fff: MSM5205 data, game only writes silence (not emulated)
}

uint8_t mpatrol::snd_port_read(uint8_t port) {
  if (port == 1) {
    if (snd_port2 & 0x08) {
      uint8_t reg = ay_addr[0] & 0x0f;
      if (reg == 14) { return sound_cmd; }  // AY 45M port A = command latch
      return ay_regs[0][reg];
    }
    if (snd_port2 & 0x10) { return ay_regs[1][ay_addr[1] & 0x0f]; }
    return 0xff;
  }
  if (port == 2) { return 0x00; }
  return 0xff;
}

// Port 2 b0 falling edge latches port 1 into the AY selected by b3/b4;
// b2 picks address (1) or data (0).
void mpatrol::snd_port_write(uint8_t port, uint8_t val) {
  if (port == 1) { snd_port1 = val; return; }
  if (port != 2) { return; }
  if ((snd_port2 & 0x01) && !(val & 0x01)) {
    if (snd_port2 & 0x04) {
      if (snd_port2 & 0x08) { ay_addr[0] = snd_port1 & 0x0f; }
      if (snd_port2 & 0x10) { ay_addr[1] = snd_port1 & 0x0f; }
    } else {
      if (snd_port2 & 0x08) {
        uint8_t reg = ay_addr[0] & 0x0f;
        ay_regs[0][reg] = snd_port1;
        if (reg < 14) { soundregs[reg] = snd_port1; }
      }
      if (snd_port2 & 0x10) {
        uint8_t reg = ay_addr[1] & 0x0f;
        ay_regs[1][reg] = snd_port1;
        if (reg < 14) { soundregs[16 + reg] = snd_port1; }
      }
    }
  }
  snd_port2 = val;
}

// ── frame ──

void mpatrol::run_frame(void) {
  current_cpu = 0;
  for (int i = 0; i < INST_PER_FRAME; i++) {
    StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    if (i == INST_PER_FRAME - 1) {
      vblank.publish([this](VideoState &s) {
        memcpy(s.vram, memory + MP_MEM_VRAM, sizeof(s.vram));
        memcpy(s.cram, memory + MP_MEM_CRAM, sizeof(s.cram));
        memcpy(s.spr, memory + MP_MEM_SPRITES, sizeof(s.spr));
        s.scroll = scroll;
        s.bg1x = bg1x; s.bg1y = bg1y;
        s.bg2x = bg2x; s.bg2y = bg2y;
        s.bgctl = bgctl;
      });
      IntZ80(&cpu[0], INT_IRQ);
    }
  }

  // M6803: 894886 Hz / 60 = 14915 cycles; MSM5205 VCK -> NMI at 4 kHz (every 224 cycles)
  int snd_budget = 14915;
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
    if (snd_cpu.timer_counter < old_tc) { snd_cpu.tcsr |= 0x20; }
    if (old_tc <= snd_cpu.timer_output_compare &&
        snd_cpu.timer_counter >= snd_cpu.timer_output_compare) {
      snd_cpu.tcsr |= 0x40;
    }
  }
}

// MAME draw order: groups 0x3c, 0x7c, 0xbc, 0xfc, each from its top entry
// down to the group base; later sprites win.
void mpatrol::prepare_frame(void) {
  vblank.read(video);

  active_sprites = 0;
  for (int group = 0x3c; group <= 0xfc; group += 0x40) {
    for (int offs = group; offs >= (group & 0xc0); offs -= 4) {
      const uint8_t *s = video.spr + offs;
      int y = spriteLine(s[0]);
      int x = spriteColumn(s[3]);
      if (y <= -16 || y >= VIS_LINES || x <= -16 || x >= OUT_WIDTH) { continue; }
      sprite_S &spr = sprite[active_sprites++];
      spr.x = x;
      spr.y = y;
      spr.code = s[2] & 0x7f;
      spr.color = s[1] & 0x0f;  // gfx has 16 colors, MAME wraps
      spr.flags = (s[1] >> 6) & 3;  // SPR_FLIPX, SPR_FLIPY
    }
  }
}

// Image rows 0..63 from ROM, 64..255 solid pen 3, pen 0 transparent.
void mpatrol::draw_bg(unsigned short *dst, int line, int img, uint8_t bgx, uint8_t bgy) {
  int iy = bgImageY(line, bgy);
  if (iy < 0) { return; }
  const unsigned short *cmap = mpatrol_bg_cmap[img];
  if (iy >= 64) {
    unsigned short c = cmap[3];
    for (int x = 0; x < OUT_WIDTH; x++) dst[x] = c;
    return;
  }
  const unsigned char *row = bgs[img] + iy * 64;
  uint8_t ix = bgImageX(0, bgx);
  for (int x = 0; x < OUT_WIDTH; x++, ix++) {
    uint8_t pen = (row[ix >> 2] >> ((3 - (ix & 3)) * 2)) & 3;
    if (pen) { dst[x] = cmap[pen]; }
  }
}

// Tile rows 0..6 are opaque, the rest has pen 0 transparent.
void mpatrol::draw_chars(unsigned short *dst, int line) {
  uint8_t ty = charY(line);
  int trow = ty >> 3;
  bool opaque = trow <= 6;
  uint8_t tx = charX(0, video.scroll, ty);
  int x = 0;
  while (x < OUT_WIDTH) {
    int px0 = tx & 7;
    int n = 8 - px0;
    if (n > OUT_WIDTH - x) { n = OUT_WIDTH - x; }
    int idx = trow * 32 + (tx >> 3);
    uint8_t attr = video.cram[idx];
    unsigned short bits = tiles[video.vram[idx] | ((attr & 0x80) << 1)][ty & 7];
    if (bits || opaque) {
      const unsigned short *cmap = mpatrol_tx_cmap[attr & 0x7f];
      for (int k = 0; k < n; k++) {
        uint8_t pen = (bits >> ((7 - (px0 + k)) * 2)) & 3;
        if (pen || opaque) { dst[x + k] = cmap[pen]; }
      }
    }
    x += n;
    tx += n;
  }
}

void mpatrol::draw_sprites(unsigned short *dst, int line) {
  for (int i = 0; i < active_sprites; i++) {
    const sprite_S &s = sprite[i];
    int r = line - s.y;
    if (r < 0 || r > 15) { continue; }
    if (s.flags & SPR_FLIPY) { r = 15 - r; }
    uint32_t bits = sprites[s.code][r];
    if (!bits) { continue; }
    const unsigned short *cmap = mpatrol_spr_cmap[s.color];
    uint8_t transp = mpatrol_spr_transp[s.color];
    bool flipx = s.flags & SPR_FLIPX;
    for (int c = 0; c < 16; c++) {
      int x = s.x + c;
      if (x < 0 || x >= OUT_WIDTH) { continue; }
      int sc = flipx ? 15 - c : c;
      uint8_t pen = (bits >> ((15 - sc) * 2)) & 3;
      if (!((transp >> pen) & 1)) { dst[x] = cmap[pen]; }
    }
  }
}

void mpatrol::render_line(unsigned short *dst, int line) {
  unsigned short black = mpatrol_spr_cmap[0][0];
  for (int x = 0; x < OUT_WIDTH; x++) dst[x] = black;

  uint8_t ctl = video.bgctl;
  if (!(ctl & 0x20)) {
    if (!(ctl & 0x10)) { draw_bg(dst, line, 0, video.bg2x, video.bg2y); }  // mountains
    // hills and city share the bg1 scroll registers, only one is drawn
    if (!(ctl & 0x02)) { draw_bg(dst, line, 1, video.bg1x, video.bg1y); }
    else if (!(ctl & 0x04)) { draw_bg(dst, line, 2, video.bg1x, video.bg1y); }
  }
  draw_chars(dst, line);
  draw_sprites(dst, line);
}

void mpatrol::render_row(short row) {
  for (int sr = 0; sr < 8; sr++) {
    unsigned short *dst = frame_buffer + sr * OUT_WIDTH;
    int line = row * 8 + sr - FIRST_ROW;
    if (line < 0 || line >= VIS_LINES) {
      memset(dst, 0, OUT_WIDTH * sizeof(unsigned short));
      continue;
    }
    render_line(dst, line);
  }
}

#ifdef LED_PIN
// logo colors: yellow title, green hills, orange ground
static const CRGB MPATROL_LED_A(0xfdd801), MPATROL_LED_B(0x28a03c), MPATROL_LED_C(0xe6965a);
static const uint8_t FIRE_BLINK = 4; // frames per blink phase

void mpatrol::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      MPATROL_LED_A, MPATROL_LED_B, MPATROL_LED_C, MPATROL_LED_C,
      MPATROL_LED_C, MPATROL_LED_B, MPATROL_LED_A};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// blink the first logo color while FIRE is held, else the menu colors
void mpatrol::gameLeds(CRGB *leds) {
  if (!(input->buttons_get() & BUTTON_FIRE)) {
    menuLeds(leds);
    return;
  }
  led_frame++;
  fill_solid(leds, NUM_LEDS, (led_frame & FIRE_BLINK) ? MPATROL_LED_A : CRGB::Black);
}
#endif

#endif // ENABLE_MPATROL
