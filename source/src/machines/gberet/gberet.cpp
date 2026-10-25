// ============================================================================
// machines/gberet/gberet.cpp
//
// Green Beret (Konami 1985, GX577), MAME konami/gberet.cpp.
// CPU: Z80 @ 3.072 MHz, sound: SN76489A @ 1.536 MHz.
// ROT0, visible 240x224: native column == frame_buffer column, native line l
// goes to galagino row l + FIRST_ROW. 64x32 tilemap with per-row scroll,
// 48 16x16 sprites; category-0 tiles cover sprites on non-transparent pens.
// ============================================================================
#include "gberet.h"
#include "../../emulation/input.h"

#include "../../catalog.h"
#if defined(GAME_GBERET) || defined(GAME_MRGOEMON)

#include "gberet_rom.h"
#include "gberet_tilemap.h"
#include "gberet_spritemap.h"
#include "gberet_cmap.h"

// action key: START with a COIN pin or the galagino controller, else EXTRA
// (without a COIN pin BUTTON_START only follows a virtual coin, 600 ms late)
#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int ACTION_KEY = BUTTON_START;
#else
static const unsigned int ACTION_KEY = BUTTON_EXTRA;
#endif

using namespace gberet_hw;

// sprite_S::flags
static const uint8_t SPR_FLIPX = 0x01;  // attr b4
static const uint8_t SPR_FLIPY = 0x02;  // attr b5
static const uint8_t SPR_HI    = 0x04;  // attr b6, code bit 8

// 3.072 MHz Z80 ~ 5000 instructions per frame, as mpatrol
static const int STEPS_PER_TICK = INST_PER_FRAME * 4 / TICKS_PER_FRAME;

gberet::gberet()
  : gberet(Assets{gberet_rom_blob, gberet_tilemap_blob, gberet_spritemap_blob,
                  gberet_tile_cmap, gberet_tile_transp, gberet_spr_cmap, gberet_spr_transp,
                  GBERET_DSW2}) { }

gberet::gberet(const Assets &a)
  : rom_asset(a.rom),
    tilemap_asset(a.tilemap),
    spritemap_asset(a.spritemap),
    tile_cmap(a.tile_cmap),
    tile_transp(a.tile_transp),
    spr_cmap(a.spr_cmap),
    spr_transp(a.spr_transp),
    dsw2(a.dsw2) {
  // order matters: ROM + tiles share one Arena block, sprites take the other
  rom_ptr = rom_asset.data();
  tiles = tilemap_asset.data();
  sprites = spritemap_asset.data();
}

void gberet::reset() {
  machineBase::reset();
  int_mask = int_ticks = 0;
  nmi_line = irq_line = false;
  sprite_bank = sn_latch = 0;
}

// ── Z80 ──

unsigned char gberet::opZ80(unsigned short Addr) {
  if (Addr < 0xc000) { return rom_ptr[Addr]; }
  return rdZ80(Addr);
}

unsigned char gberet::rdZ80(unsigned short Addr) {
  if (Addr < 0xc000) { return rom_ptr[Addr]; }
  if (Addr < 0xe000) { return memory[Addr - 0xc000]; }
  if (Addr < 0xe040) { return memory[GB_MEM_SCROLL + (Addr - 0xe000)]; }
  switch (Addr) {
    case 0xf200:
      return dsw2 | (input->demoSoundsOff() ? GBERET_DSW2_DEMO_SOUND_OFF : 0);
    case 0xf400: return GBERET_DSW3;
    case 0xf600: return GBERET_DSW1;
    case 0xf602: {  // P1, KONAMI8_MONO_B12_UNK
      const unsigned int keys = input->buttons_get();
      uint8_t v = 0xff;
      if (keys & BUTTON_LEFT)  { v &= ~0x01; }
      if (keys & BUTTON_RIGHT) { v &= ~0x02; }
      if (keys & BUTTON_UP)    { v &= ~0x04; }  // jump
      if (keys & BUTTON_DOWN)  { v &= ~0x08; }  // crouch
      if (keys & BUTTON_FIRE)  { v &= ~0x10; }  // BUTTON1 knife
      // BUTTON2 special weapon: START (ACTION_KEY), or B/L1/R1 on the controller
      if (keys & (ACTION_KEY | BUTTON_B | BUTTON_L1 | BUTTON_R1)) { v &= ~0x20; }
      return v;
    }
    case 0xf603: {  // SYSTEM, KONAMI8_SYSTEM_UNK
      const unsigned int keys = input->buttons_get();
      uint8_t v = 0xff;
      if (keys & BUTTON_COIN)  { v &= ~0x01; }
      if (keys & BUTTON_START) { v &= ~0x08; }
      return v;
    }
    default: return 0xff;  // f601 P2 (cocktail), unmapped
  }
}

void gberet::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr < 0xc000) { return; }
  if (Addr < 0xe000) {
    memory[Addr - 0xc000] = Value;
    return;
  }
  if (Addr < 0xe040) {
    memory[GB_MEM_SCROLL + (Addr - 0xe000)] = Value;
    return;
  }
  switch (Addr) {
    case 0xe043: sprite_bank = Value; return;
    case 0xe044: {
      // clearing an enable bit acknowledges its held line; b3 flip ignored (upright)
      const uint8_t ack = ~Value & int_mask;
      if (ack & 0x01) { nmi_line = false; }
      if (ack & 0x06) { irq_line = false; }
      int_mask = Value & 0x07;
      if (int_mask & 0x01) { game_started = 1; }
      return;
    }
    case 0xf200: sn_latch = Value; return;
    case 0xf400: sn[0].write(sn_latch); return;
    default: return;  // e040-e042 unknown, f000 coin counters, f600 watchdog
  }
}

// ── frame ──

void gberet::interrupt_tick() {
  if (nmiTick(int_ticks, int_mask) && !nmi_line) {
    nmi_line = true;
    IntZ80(&cpu[0], INT_NMI);
  }
  if (irqTick(int_ticks, int_mask)) { irq_line = true; }
  int_ticks++;
}

void gberet::run_frame(void) {
  current_cpu = 0;
  for (int t = 0; t < TICKS_PER_FRAME; t++) {
    if (t == VBLANK_TICK) {
      vblank.publish([this](VideoState &s) {
        const int vis = FIRST_TILE_ROW * 64;
        memcpy(s.cram, memory + GB_MEM_CRAM + vis, sizeof(s.cram));
        memcpy(s.vram, memory + GB_MEM_VRAM + vis, sizeof(s.vram));
        memcpy(s.spr, memory + ((sprite_bank & 0x08) ? GB_MEM_SPR2 : GB_MEM_SPR), sizeof(s.spr));
        for (int r = 0; r < VIS_TILE_ROWS; r++) {
          s.scroll[r] = rowScroll(memory + GB_MEM_SCROLL, r + FIRST_TILE_ROW);
        }
      });
    }
    interrupt_tick();
    for (int i = 0; i < STEPS_PER_TICK; i++) {
      // level-triggered IRQ: taken whenever enabled while held (IM 1)
      if (irq_line && (cpu[0].IFF & IFF_1)) { IntZ80(&cpu[0], INT_IRQ); }
      StepZ80(&cpu[0]);
    }
  }
}

// MAME order: offsets ascending, later sprites win.
void gberet::prepare_frame(void) {
  vblank.read(video);

  active_sprites = 0;
  for (int offs = 0; offs < SPRITE_BYTES; offs += 4) {
    const uint8_t *s = video.spr + offs;
    if (!s[3]) { continue; }
    const int x = spriteColumn(s[2], s[1]);
    const int y = spriteLine(s[3]);
    if (x <= -16 || x >= OUT_WIDTH || y <= -16 || y >= VIS_LINES) { continue; }
    sprite_S &spr = sprite[active_sprites++];
    spr.x = x;
    spr.y = y;
    spr.code = s[0];
    spr.color = s[1] & 0x0f;
    spr.flags = ((s[1] >> 4) & 3) | ((s[1] & 0x40) ? SPR_HI : 0);  // SPR_FLIPX, SPR_FLIPY
  }
}

// Tiles opaque; front[x] marks category-0 pixels on non-transparent pens, which
// MAME redraws over the sprites. Sprites then skip those pixels.
void gberet::render_line(unsigned short *dst, int line) {
  uint8_t front[OUT_WIDTH];
  const int trow = tileRow(line) - FIRST_TILE_ROW;
  const int ty = tileLine(line);
  const uint8_t *cram = video.cram + trow * 64;
  const uint8_t *vram = video.vram + trow * 64;
  uint16_t tx = tileX(0, video.scroll[trow]);
  int x = 0;
  while (x < OUT_WIDTH) {
    const int px0 = tx & 7;
    int n = 8 - px0;
    if (n > OUT_WIDTH - x) { n = OUT_WIDTH - x; }
    const int idx = tx >> 3;
    const uint8_t attr = cram[idx];
    const uint32_t bits = tiles[vram[idx] | ((attr & 0x40) << 2)][(attr & 0x20) ? 7 - ty : ty];
    const unsigned short *cmap = tile_cmap[attr & 0x0f];
    const uint16_t transp = tile_transp[attr & 0x0f];
    const bool over = !(attr & 0x80);
    for (int k = 0; k < n; k++) {
      const int px = (attr & 0x10) ? 7 - (px0 + k) : px0 + k;
      const uint8_t pen = (bits >> ((7 - px) * 4)) & 15;
      dst[x + k] = cmap[pen];
      front[x + k] = over && !((transp >> pen) & 1);
    }
    x += n;
    tx = (tx + n) & 511;
  }

  for (int i = 0; i < active_sprites; i++) {
    const sprite_S &s = sprite[i];
    int r = line - s.y;
    if (r < 0 || r > 15) { continue; }
    if (s.flags & SPR_FLIPY) { r = 15 - r; }
    const uint32_t *bits = sprites[s.code | ((s.flags & SPR_HI) << 6)][r];
    const uint16_t transp = spr_transp[s.color];
    if (!(bits[0] | bits[1]) && (transp & 1)) { continue; }
    const unsigned short *cmap = spr_cmap[s.color];
    const bool flipx = s.flags & SPR_FLIPX;
    for (int c = 0; c < 16; c++) {
      const int px = s.x + c;
      if (px < 0 || px >= OUT_WIDTH || front[px]) { continue; }
      const int sc = flipx ? 15 - c : c;
      const uint8_t pen = (bits[sc >> 3] >> ((7 - (sc & 7)) * 4)) & 15;
      if (!((transp >> pen) & 1)) { dst[px] = cmap[pen]; }
    }
  }
}

void gberet::render_row(short row) {
  for (int sr = 0; sr < 8; sr++) {
    unsigned short *dst = frame_buffer + sr * OUT_WIDTH;
    const int line = row * 8 + sr - FIRST_ROW;
    if (line < 0 || line >= VIS_LINES) {
      memset(dst, 0, OUT_WIDTH * sizeof(unsigned short));
      continue;
    }
    render_line(dst, line);
  }
}

#ifdef LED_PIN
// logo colors (quantized logos/gberet.png): olive, orange, dark olive, red
static const CRGB GBERET_LED_A(0x5a572b), GBERET_LED_B(0x886126),
                  GBERET_LED_C(0x4e4b26), GBERET_LED_RED(0x9e2b1e);

void gberet::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      GBERET_LED_C, GBERET_LED_A, GBERET_LED_B, GBERET_LED_RED,
      GBERET_LED_B, GBERET_LED_A, GBERET_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// blink red while the knife (FIRE) is held, else the menu colors
void gberet::gameLeds(CRGB *leds) {
  fireBlinkLeds(leds, input, led_frame, GBERET_LED_RED, menuLeds);
}
#endif

#endif // GAME_GBERET || GAME_MRGOEMON
