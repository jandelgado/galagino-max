#include "1943.h"
#include "1943_rom.h"
#include "1943_sound.h"
#include "1943_chars.h"
#include "1943_bg1.h"
#include "1943_bg2.h"
#include "1943_sprites.h"
#include "1943_tilemap.h"
#include "1943_cmap.h"
#include "1943_dipswitches.h"
#include "../../emulation/spriteflip.h"

// main Z80 6 MHz: twice commando's 3 MHz budget per quarter frame
static const uint16_t MAIN_STEPS = 2 * INST_PER_FRAME;
static const uint16_t ROM_END = 0x8000;
static const uint16_t BANK_END = 0xc000;
static const uint16_t BANK_SIZE = 0x4000;

// sound Z80: 3 MHz, IRQ 4 x per frame (MAME 4 * 60 Hz). The IRQ handler
// returns to an "EI; JR $-1" idle loop at $007B; stepping it is wasted.
static const uint16_t SOUND_ROM_END = 0x8000;
static const uint16_t SOUND_IRQS = 4;
static const uint16_t SOUND_STEPS = 12500 / 4; // 3 MHz / 240 Hz, >= 4 cycles each
static const uint16_t SOUND_IDLE_PC = 0x007c;
static const uint32_t FM_RATE = 24000;         // audio.cpp I2S rate
static const int FM_SHIFT = 9; // full-scale chip -> 64, an SSG channel peaks at 75

static const uint8_t SND_PLAYER_DOWN = 0x05; // sound latch: player plane explodes

// $C804
static const uint8_t C804_BANK_SHIFT = 2;
static const uint8_t C804_BANK_MASK = 7;
static const uint8_t C804_SOUND_RESET = 0x20;
static const uint8_t C804_CHARS = 0x80;

// $D806 layer enables
static const uint8_t D806_BG1 = 0x10;
static const uint8_t D806_BG2 = 0x20;
static const uint8_t D806_SPRITES = 0x40;

// tilemaps: 2048 x 8 tiles of 32x32, SCAN_COLS, 2 bytes each; bg1 map at
// 0, bg2 at $8000. bg2 gfx has 128 tiles. attr b6 flip x, b7 flip y.
static const uint8_t TILE_ROWS = 8;
static const uint16_t BG2_MAP = 0x8000;
static const uint8_t BG2_CODE_MASK = 0x7f;
static const uint8_t ATTR_FLIPX = 0x40;
static const uint8_t ATTR_FLIPY = 0x80;

// native visible area -> tilemap: MAME scrolldx 128 / scrolldy 6 against
// the visible area x 128.., y 22..; sprites drawn at y + 6 the same way
static const int TILEMAP_Y_OFFSET = 16;

// sprite attribute: b7-5 code bits 10-8, b4 x bit 8 (subtracted), b3-0 color
static const uint8_t SPR_CODE_HI = 0xe0;
static const uint8_t SPR_SX_HI = 0x10;

_1943::_1943()
  : _1943_tilemap(_1943_tilemap_blob),
    _1943_bg2(_1943_bg2_blob),
    _1943_chars(_1943_chars_blob),
    _1943_cmap_chars(_1943_cmap_chars_blob),
    _1943_cmap_bg1(_1943_cmap_bg1_blob),
    _1943_cmap_bg2(_1943_cmap_bg2_blob),
    _1943_cmap_sprites(_1943_cmap_sprites_blob),
    _1943_pen_masks(_1943_pen_masks_blob),
    _1943_rom(_1943_rom_blob),
    _1943_sound(_1943_sound_blob),
    _1943_bg1(_1943_bg1_blob),
    _1943_sprites(_1943_sprites_blob) {
  // this order only (first fit): A tilemap, B bg2 and chars, A color maps
  tilemap = _1943_tilemap.data();
  bg2_tiles = _1943_bg2.data();
  chars = _1943_chars.data();
  cmap_chars = _1943_cmap_chars.data();
  cmap_bg1 = _1943_cmap_bg1.data();
  cmap_bg2 = _1943_cmap_bg2.data();
  cmap_sprites = _1943_cmap_sprites.data();
  bg1_clear = _1943_pen_masks.data()[0];
  spr_behind = _1943_pen_masks.data()[1];
  rom = _1943_rom.data();
  sound_rom = _1943_sound.data();
  bg1_tiles = _1943_bg1.data();
  sprite_gfx = _1943_sprites.data();

  // skip fully transparent chars (most of the text layer)
  for (uint16_t c = 0; c < _1943_chars.size(); c++) {
    bool blank = true;
    for (int r = 0; r < 8; r++) {
      blank = blank && chars[c][r] == 0;
    }
    if (blank) {
      blank_chars[c >> 5] |= 1u << (c & 31);
    }
  }
}

void _1943::reset() {
  machineBase::reset();
  c804 = 0;
  d806 = 0;
  memset(scroll, 0, sizeof(scroll));
  sound_latch = 0;
  irq_pending = false;
  sound_reset = false;
  ym[0].reset();
  ym[1].reset();
  fm[0].reset();
  fm[1].reset();
}

unsigned char _1943::opZ80(unsigned short Addr) {
  return rdZ80(Addr);
}

unsigned char _1943::rdZ80(unsigned short Addr) {
  if (current_cpu) {
    return rdSound(Addr);
  }
  if (Addr < ROM_END) {
    return rom[Addr];
  }
  if (Addr < BANK_END) {
    const uint8_t bank = (c804 >> C804_BANK_SHIFT) & C804_BANK_MASK;
    return rom[ROM_END + bank * BANK_SIZE + Addr - ROM_END];
  }
  if (Addr >= 0xe000) {
    return memory[RAM + Addr - 0xe000];
  }
  if (Addr >= 0xd000 && Addr < 0xd800) {
    return memory[FG_CODE + Addr - 0xd000];
  }
  if (Addr >= 0xd800 && Addr <= 0xd804) {
    return scroll[Addr - 0xd800];
  }
  return rdInput(Addr);
}

unsigned char _1943::rdInput(unsigned short Addr) {
  const unsigned int keys = input->buttons_get();
  switch (Addr) {
    case 0xc000: { // SYSTEM: b6 COIN1, b0 START1, b3 VBLANK (unused, high)
      unsigned char v = 0xff;
      if (keys & BUTTON_COIN) { // level: the game debounces it
        v &= ~0x40;
      }
      if (keys & BUTTON_START) {
        v &= ~0x01;
      }
      return v;
    }
    case 0xc001: { // P1: b5 button 2, b4 fire, b3 U, b2 D, b1 L, b0 R
      unsigned char v = 0xff;
      if (keys & BUTTON_RIGHT) {
        v &= ~0x01;
      }
      if (keys & BUTTON_LEFT) {
        v &= ~0x02;
      }
      if (keys & BUTTON_DOWN) {
        v &= ~0x04;
      }
      if (keys & BUTTON_UP) {
        v &= ~0x08;
      }
      if (keys & (BUTTON_FIRE | BUTTON_L1)) {
        v &= ~0x10;
      }
      if (keys & (BUTTON_START | BUTTON_R1)) {
        v &= ~0x20;
      }
      return v;
    }
    case 0xc003:
      return _1943_DIP_A;
    case 0xc004:
      return _1943_DIP_B;
    case 0xc007: // bootleg: protection patched out, expects 0
      return 0x00;
  }
  return 0xff;
}

void _1943::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu) {
    wrSound(Addr, Value);
    return;
  }
  if (Addr >= 0xe000) {
    memory[RAM + Addr - 0xe000] = Value;
    return;
  }
  if (Addr >= 0xd000 && Addr < 0xd800) {
    memory[FG_CODE + Addr - 0xd000] = Value;
    return;
  }
  if (Addr >= 0xd800 && Addr <= 0xd804) {
    scroll[Addr - 0xd800] = Value;
    return;
  }

  switch (Addr) {
    case 0xc800:
#ifdef LED_PIN
      if (Value == SND_PLAYER_DOWN) {
        led_flash = LED_FLASH_FRAMES;
      }
#endif
      sound_latch = Value;
      game_started = 1; // first command comes right after the boot test
      return;
    case 0xc804: { // also coin counters, flip (upright: unused)
      const bool hold = Value & C804_SOUND_RESET;
      if (hold && !sound_reset) {
        ResetZ80(&cpu[1]);
      }
      sound_reset = hold;
      c804 = Value;
      return;
    }
    case 0xd806:
      d806 = Value;
      return;
  }
}

unsigned char _1943::rdSound(unsigned short Addr) {
  if (Addr < SOUND_ROM_END) {
    return sound_rom[Addr];
  }
  if (Addr >= 0xc000 && Addr < 0xc800) {
    return memory[SOUND_RAM + Addr - 0xc000];
  }
  if (Addr == 0xc800) {
    return sound_latch;
  }
  return 0x00; // $D800 MCU latch: no MCU on the bootleg
}

// $E000/$E001 YM2203 #1, $E002/$E003 #2: even = address, odd = data.
// Both YM2203 ports go to the chip (Ym2203: SSG registers 0-15, FM queue
// for the rest). The game sets prescaler 1/3: SSG 750 kHz (AY_INC).
void _1943::wrSound(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0xc000 && Addr < 0xc800) {
    memory[SOUND_RAM + Addr - 0xc000] = Value;
    return;
  }
  if (Addr < 0xe000 || Addr > 0xe003) {
    return;
  }

  const uint8_t chip = (Addr >> 1) & 1;
  if (!(Addr & 1)) {
    ym[chip].writeAddress(Value);
    return;
  }
  ym[chip].writeData(Value);
}

void _1943::runSound() {
  if (sound_reset) {
    return;
  }
  current_cpu = 1;
  Z80 *z = &cpu[1];
  IntZ80(z, INT_RST38);
  for (int i = 0; i < SOUND_STEPS; i++) {
    if (z->PC.W == SOUND_IDLE_PC && (z->IFF & IFF_1)) {
      break;
    }
    StepZ80(z);
  }
  current_cpu = 0;
}

void _1943::run_frame() {
  for (int q = 0; q < SOUND_IRQS; q++) {
    current_cpu = 0;
    for (int i = 0; i < MAIN_STEPS; i++) {
      // irq0_line_hold: the main loop runs with DI and only briefly EI
      if (irq_pending && (cpu[0].IFF & IFF_1)) {
        IntZ80(&cpu[0], INT_RST38);
        irq_pending = false;
      }
      StepZ80(&cpu[0]);
    }
    runSound();
  }

  latchVideo();
  irq_pending = true;
}

// sprite RAM and scroll as the vblank IRQ sees them
void _1943::latchVideo() {
  vblank.publish([this](VideoState &v) {
    for (uint8_t s = 0; s < SPRITES; s++) {
      memcpy(v.sprites[s], memory + SPRITE_RAM + 32 * s, 4);
    }
    v.bg1_x = scroll[0] | (scroll[1] << 8);
    v.bg1_y = scroll[2];
    v.bg2_x = scroll[3] | (scroll[4] << 8);
    v.c804 = c804;
    v.d806 = d806;
  });
}

void _1943::renderFm(int16_t *out, int n) {
  static const int CHUNK = 64; // Audio renders 64 samples per call
  for (int done = 0; done < n; done += CHUNK) {
    const int len = n - done < CHUNK ? n - done : CHUNK;
    int32_t acc[CHUNK] = {};
    fm[0].render(acc, len, FM_RATE);
    fm[1].render(acc, len, FM_RATE);
    for (int i = 0; i < len; i++) {
      out[done + i] = (int16_t)(acc[i] >> FM_SHIFT);
    }
  }
}

// MAME draw order: entry 0 first, and the first sprite to cover a pixel
// keeps it, so the list stays in entry order
void _1943::prepare_frame() {
  vblank.read(video);

  spr_count = 0;
  if (!(video.d806 & D806_SPRITES)) {
    return;
  }
  for (uint8_t s = 0; s < SPRITES; s++) {
    const uint8_t *e = video.sprites[s];
    const int sx = e[3] - ((e[1] & SPR_SX_HI) << 4);
    const int vy = e[2] - TILEMAP_Y_OFFSET;
    if (sx <= -16 || vy <= -16 || vy >= WIDTH) {
      continue;
    }
    spr_list[spr_count++] = s;
  }
}

// One panel line = one native column vx of a 64K x 256 tile plane, walked
// along native y. Tiles are 32x32, stored as native-x lines of 4 words.
// The pixel loops take a word (8 pens) at a time and keep their state in
// registers: per-pen indexing cost ~36 cycles a pixel on the ESP32.

// 32 pens of a tile line in panel order, pen 0 in the low nibble of w[0]
__attribute__((always_inline)) static inline void tilePens(uint32_t w[4], const uint32_t *line,
                                                           bool flip) {
  if (!flip) {
    memcpy(w, line, 4 * sizeof(uint32_t));
    return;
  }
  for (int k = 0; k < 4; k++) {
    w[k] = rev4bpp(line[3 - k]);
  }
}

// pens [from, to) of a tile line, opaque
__attribute__((always_inline)) static inline void drawPens(uint16_t *dst, const uint32_t w[4],
                                                           int from, int to,
                                                           const unsigned short *colors) {
  while (from < to) {
    const int n = ((from | 7) + 1 < to ? (from | 7) + 1 : to) - from;
    uint32_t p = w[from >> 3] >> ((from & 7) << 2);
    from += n;
    for (int i = 0; i < n; i++, p >>= 4) {
      *dst++ = colors[p & 15];
    }
  }
}

// same, skipping the pens set in clear; returns a bit per opaque pixel,
// bit 0 = dst[0]
__attribute__((always_inline)) static inline uint32_t drawPensMasked(
    uint16_t *dst, const uint32_t w[4], int from, int to, const unsigned short *colors,
    uint32_t clear) {
  uint32_t opaque = 0;
  uint32_t bit = 1;
  while (from < to) {
    const int n = ((from | 7) + 1 < to ? (from | 7) + 1 : to) - from;
    uint32_t p = w[from >> 3] >> ((from & 7) << 2);
    from += n;
    for (int i = 0; i < n; i++, p >>= 4, bit <<= 1, dst++) {
      const uint32_t pen = p & 15;
      if (clear & (1u << pen)) {
        continue;
      }
      *dst = colors[pen];
      opaque |= bit;
    }
  }
  return opaque;
}

// ORs a span's opaque bits into a line mask at pixel u
static inline void addMask(uint32_t *mask, int u, uint32_t opaque) {
  const int shift = u & 31;
  mask[u >> 5] |= opaque << shift;
  const uint32_t spill = shift ? opaque >> (32 - shift) : 0;
  if (spill) {
    mask[(u >> 5) + 1] |= spill;
  }
}

__attribute__((optimize("O2"))) void _1943::blitBg2Line(uint16_t *dst, int vx) {
  const uint16_t tx = vx + video.bg2_x;
  const unsigned char *map = tilemap + BG2_MAP + 2 * (tx >> 5) * TILE_ROWS;

  int ty = TILEMAP_Y_OFFSET;
  for (int u = 0; u < WIDTH;) {
    const int trow = ty >> 5; // no y scroll: 16..239 stays inside the map
    const uint8_t attr = map[2 * trow + 1];
    const uint8_t code = map[2 * trow] & BG2_CODE_MASK;
    const uint32_t *line = bg2_tiles[code] + 4 * ((attr & ATTR_FLIPX) ? 31 - (tx & 31) : tx & 31);
    uint32_t w[4];
    tilePens(w, line, attr & ATTR_FLIPY);

    const int from = ty & 31;
    const int to = from + WIDTH - u < 32 ? from + WIDTH - u : 32;
    drawPens(dst + u, w, from, to, cmap_bg2[(attr >> 2) & 15]);
    u += to - from;
    ty = (ty | 31) + 1;
  }
}

// bg1 pens whose lookup is 0x0F are transparent; mask gets the opaque ones
__attribute__((optimize("O2"))) void _1943::blitBg1Line(uint16_t *dst, int vx, uint32_t *mask) {
  const uint16_t tx = vx + video.bg1_x;
  const unsigned char *map = tilemap + 2 * (tx >> 5) * TILE_ROWS;

  int ty = TILEMAP_Y_OFFSET + video.bg1_y;
  for (int u = 0; u < WIDTH;) {
    const int trow = (ty >> 5) & (TILE_ROWS - 1);
    const uint8_t attr = map[2 * trow + 1];
    const uint16_t code = map[2 * trow] | ((attr & 1) << 8);
    const uint32_t *line = bg1_tiles[code] + 4 * ((attr & ATTR_FLIPX) ? 31 - (tx & 31) : tx & 31);
    uint32_t w[4];
    tilePens(w, line, attr & ATTR_FLIPY);

    const uint8_t color = (attr >> 2) & 15;
    const int from = ty & 31;
    const int to = from + WIDTH - u < 32 ? from + WIDTH - u : 32;
    addMask(mask, u, drawPensMasked(dst + u, w, from, to, cmap_bg1[color], bg1_clear[color]));
    u += to - from;
    ty = (ty | 31) + 1;
  }
}

// a pixel taken by an earlier sprite stays taken, even where bg1 hides it
void _1943::blitSprites(short row) {
  const int vx_top = 255 - 8 * (row - FIRST_ROW); // native x of strip line 0
  memset(drawn, 0, sizeof(drawn));

  for (uint8_t n = 0; n < spr_count; n++) {
    const uint8_t *e = video.sprites[spr_list[n]];
    const int sx = e[3] - ((e[1] & SPR_SX_HI) << 4);
    if (sx > vx_top || sx + 15 < vx_top - 7) {
      continue;
    }
    const uint16_t code = e[0] | ((e[1] & SPR_CODE_HI) << 3);
    const uint8_t color = e[1] & 15;
    const unsigned short *colors = cmap_sprites[color];
    const uint16_t behind = spr_behind[color];
    const int vy = e[2] - TILEMAP_Y_OFFSET;

    for (int l = 0; l < 8; l++) {
      const int sl = vx_top - l - sx; // sprite line = native x inside
      if (sl < 0 || sl > 15) {
        continue;
      }
      const uint32_t *line = sprite_gfx[code] + 2 * sl;
      uint16_t *dst = frame_buffer + WIDTH * l;
      for (int i = 0; i < 16; i++) {
        const int u = vy + i;
        if (u < 0 || u >= WIDTH) {
          continue;
        }
        const uint32_t bit = 1u << (u & 31);
        const uint8_t pen = (line[i >> 3] >> ((i & 7) << 2)) & 15;
        if (!pen || (drawn[l][u >> 5] & bit)) {
          continue;
        }
        drawn[l][u >> 5] |= bit;
        if ((bg1_mask[l][u >> 5] & bit) && (behind & (1 << pen))) {
          continue;
        }
        dst[u] = colors[pen];
      }
    }
  }
}

// fg text, not scrolled: strip row = char column 31 - (row - FIRST_ROW),
// panel column col = char row col + 2 (16 lines above the visible area)
void _1943::blitChar(short row, int col) {
  const uint16_t idx = (col + TILEMAP_Y_OFFSET / 8) * 32 + (31 - (row - FIRST_ROW));
  const uint8_t attr = memory[FG_ATTR + idx];
  const uint16_t code = memory[FG_CODE + idx] | ((attr & 0xe0) << 3);
  if (blank_chars[code >> 5] & (1u << (code & 31))) {
    return;
  }

  const unsigned short *colors = cmap_chars[attr & 31];
  for (int l = 0; l < 8; l++) {
    uint16_t pix = chars[code][7 - l];
    uint16_t *dst = frame_buffer + WIDTH * l + 8 * col;
    for (int i = 0; i < 8; i++, pix >>= 2) {
      if (pix & 3) {
        dst[i] = colors[pix & 3];
      }
    }
  }
}

// layers bottom to top: bg2 (sea), bg1 (clouds), sprites, fg text
void _1943::render_row(short row) {
  if (row < FIRST_ROW || row >= LAST_ROW) {
    return;
  }

  memset(bg1_mask, 0, sizeof(bg1_mask));
  for (int l = 0; l < 8; l++) {
    const int vx = 255 - (8 * (row - FIRST_ROW) + l);
    uint16_t *dst = frame_buffer + WIDTH * l;
    if (video.d806 & D806_BG2) {
      blitBg2Line(dst, vx);
    }
    if (video.d806 & D806_BG1) {
      blitBg1Line(dst, vx, bg1_mask[l]);
    }
  }

  if (spr_count) {
    blitSprites(row);
  }

  if (!(video.c804 & C804_CHARS)) {
    return;
  }
  for (int col = 0; col < WIDTH / 8; col++) {
    blitChar(row, col);
  }
}

#ifdef LED_PIN
// logo colors (quantized logos/1943.png)
static const CRGB RED = CRGB(0xc60000);
static const CRGB BLUE = CRGB(0x006abe);
static const CRGB DARK_BLUE = CRGB(0x00417c);
static const CRGB WHITE = CRGB(0xf7fbf7);

void _1943::menuLeds(CRGB *leds) {
  const CRGB pattern[NUM_LEDS] = {DARK_BLUE, BLUE, RED, WHITE, RED, BLUE, DARK_BLUE};
  memcpy(leds, pattern, sizeof(pattern));
}

// logo colors, red while the player's plane explodes
void _1943::gameLeds(CRGB *leds) {
  if (led_flash) {
    led_flash--;
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = LED_RED;
    }
    return;
  }
  menuLeds(leds);
}
#endif

// YM2203 FM plus the SSG parts, clipped
void _1943::renderAudio(int16_t *out, int n) {
  int16_t ssg[AUDIO_CHUNK];
  renderFm(out, n);
  Ym2203::renderSsgBank(ym, ssg, n);
  audioAdd(out, ssg, n);
  audioClampAll(out, n, AUDIO_MAX);
}
