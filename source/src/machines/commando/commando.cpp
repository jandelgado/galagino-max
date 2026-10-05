#include "commando.h"
#include "commando_rom.h"
#include "commando_chars.h"
#include "commando_tiles.h"
#include "commando_sprites0.h"
#include "commando_sprites1.h"
#include "commando_sprites2.h"
#include "commando_cmap.h"
#include "commando_sid.h"
#include "commando_dipswitches.h"
#include "commando_music.h"
#include "../../emulation/arena.h"
#include "../../emulation/spriteflip.h"

static const uint16_t ROM_END = 0xc000;
static const uint16_t MAIN_STEPS = INST_PER_FRAME * 4; // 3 MHz main Z80

// bg/fg attribute bits (MAME TILE_FLIPYX((attr & 0x30) >> 4))
static const uint8_t ATTR_FLIPX = 0x10;
static const uint8_t ATTR_FLIPY = 0x20;

// sprite attribute bits
static const uint8_t SPR_SX_HI = 0x01;
static const uint8_t SPR_FLIPX = 0x04;
static const uint8_t SPR_FLIPY = 0x08;
static const uint8_t SPR_BANK_HIDDEN = 3;

static const uint8_t SND_DEATH = 0x2b; // sound latch: player death jingle

static const uint8_t PEN_SPRITE_CLEAR = 15;
static const uint8_t PEN_CHAR_CLEAR = 3;

// native visible area -> tilemap: MAME scrolldx 128 / scrolldy 6 against
// the visible area x 128.., y 22.. (verified against MAME snapshots)
static const int BG_TY_OFFSET = 16;
static const int FG_ROW_OFFSET = 2; // 16 lines = 2 char rows
static const int SPRITE_Y_OFFSET = 16;

// opcodes (M1 fetches) are encrypted, except address 0
static inline uint8_t decrypt(uint8_t b) {
  return (b & 0x11) | ((b & 0xe0) >> 4) | ((b & 0x0e) << 4);
}

commando::commando()
  : commando_rom(commando_rom_blob),
    commando_sprites0(commando_sprites0_blob),
    commando_sprites1(commando_sprites1_blob),
    commando_sprites2(commando_sprites2_blob),
    commando_chars(commando_chars_blob),
    commando_cmap_tiles(commando_cmap_tiles_blob),
    commando_cmap_sprites(commando_cmap_sprites_blob),
    commando_cmap_chars(commando_cmap_chars_blob),
    commando_tiles(commando_tiles_blob),
    commando_sid(commando_sid_blob) {
  // largest first: block A rom + sprites0, B sprites1 + 2 + chars
  rom = commando_rom.data();
  sprite_banks[0] = commando_sprites0.data();
  sprite_banks[1] = commando_sprites1.data();
  sprite_banks[2] = commando_sprites2.data();
  chars = commando_chars.data();
  cmap_tiles = commando_cmap_tiles.data();
  cmap_sprites = commando_cmap_sprites.data();
  cmap_chars = commando_cmap_chars.data();
  tiles = commando_tiles.data();

  // skip fully transparent chars (most of the text layer) per tile
  for (uint16_t c = 0; c < commando_chars.size(); c++) {
    bool blank = true;
    for (int r = 0; r < 8; r++) {
      blank = blank && chars[c][r] == 0xffff;
    }
    if (blank) {
      blank_chars[c >> 5] |= 1u << (c & 31);
    }
  }

  tune_ok = psidParse(commando_sid.data(), commando_sid.size(), &tune);
  if (tune_ok) {
    tune_ok = player.load(tune, Arena::alloc<uint8_t>(TUNE_RAM_SIZE), TUNE_RAM_SIZE);
  }
  if (!tune_ok) {
    printf("commando: Commando.sid is not a supported PSID\n");
  }
}

void commando::reset() {
  machineBase::reset();
  memset(scroll, 0, sizeof(scroll));
  music_subtune = 0;
  music_request = 0;
}

unsigned char commando::opZ80(unsigned short Addr) {
  if (Addr >= ROM_END) {
    return rdZ80(Addr);
  }
  return Addr ? decrypt(rom[Addr]) : rom[Addr];
}

unsigned char commando::opPrefixZ80(unsigned short Addr) {
  return opZ80(Addr);
}

unsigned char commando::rdZ80(unsigned short Addr) {
  if (Addr < ROM_END) {
    return rom[Addr];
  }
  if (Addr >= 0xe000) {
    return memory[RAM + Addr - 0xe000];
  }
  if (Addr >= 0xd000) {
    return memory[VRAM + Addr - 0xd000];
  }

  const unsigned int keys = input->buttons_get();
  switch (Addr) {
    case 0xc000: { // SYSTEM: b7 COIN1, b0 START1
      unsigned char v = 0xff;
      if (keys & BUTTON_COIN) { // level: the game debounces it
        v &= ~0x80;
      }
      if (keys & BUTTON_START) {
        v &= ~0x01;
      }
      return v;
    }
    case 0xc001: { // P1: b5 grenade, b4 fire, b3 U, b2 D, b1 L, b0 R
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
      return COMMANDO_DIP_A;
    case 0xc004:
      return COMMANDO_DIP_B;
  }
  return 0xff;
}

void commando::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0xe000) {
    memory[RAM + Addr - 0xe000] = Value;
    return;
  }
  if (Addr >= 0xd000) {
    memory[VRAM + Addr - 0xd000] = Value;
    return;
  }
  if (Addr == 0xc800) {
#ifdef LED_PIN
    if (Value == SND_DEATH) {
      led_flash = LED_FLASH_FRAMES;
    }
#endif
    decodeMusic(Value);
    return;
  }
  if (Addr >= 0xc808 && Addr <= 0xc80b) {
    scroll[Addr - 0xc808] = Value;
  }
  // $C804 coin counters, sound CPU reset, flip (upright: unused), $C806 DMA
}

// sound latch: music commands switch the SID tune, effects are ignored
void commando::decodeMusic(uint8_t command) {
  const MusicCue cue = commandoMusicCue(command);
  if (cue.action == MUSIC_NONE) {
    return;
  }

  // a running tune keeps playing (game start sends 20 21 20 21)
  const uint8_t subtune = cue.action == MUSIC_PLAY ? cue.subtune : 0;
  if (subtune == music_subtune) {
    return;
  }
  music_subtune = subtune;
  music_request = (uint16_t)(((music_request >> 8) + 1) << 8) | subtune;
}

void commando::run_frame() {
  game_started = 1;

  current_cpu = 0;
  for (int i = 0; i < MAIN_STEPS; i++) {
    StepZ80(&cpu[0]);
  }

  // hardware buffers sprite RAM on vblank, right before the IRQ
  vblank.publish([this](VideoState &v) {
    memcpy(v.sprites, memory + SPRITE_RAM, sizeof(v.sprites));
    memcpy(v.bg, memory + BG_RAM, sizeof(v.bg));
    v.scroll_x = scroll[0] | (scroll[1] << 8);
    v.scroll_y = scroll[2] | (scroll[3] << 8);
  });
  IntZ80(&cpu[0], INT_RST10);
}

void commando::prepare_frame() {
  vblank.read(video);

  // MAME draw order: offset 380 first, so lower offsets end up on top
  active_sprites = 0;
  for (int offs = SPRITE_BYTES - 4; offs >= 0; offs -= 4) {
    const uint8_t *s = video.sprites + offs;
    const uint8_t attr = s[1];
    const uint8_t bank = attr >> 6;
    if (bank == SPR_BANK_HIDDEN) {
      continue;
    }

    const int sx = s[3] - ((attr & SPR_SX_HI) << 8);
    sprite_S spr;
    spr.x = s[2] - SPRITE_Y_OFFSET;
    spr.y = 8 * FIRST_ROW + 255 - 15 - sx; // top row of the sprite
    if (spr.x <= -16 || spr.x >= WIDTH || spr.y <= 8 * FIRST_ROW - 16 || spr.y >= 8 * LAST_ROW) {
      continue;
    }
    spr.code = s[0];
    spr.color_block = bank;
    spr.color = (attr >> 4) & 3;
    // native x is panel rows, native y panel columns
    spr.flags = ((attr & SPR_FLIPX) ? FLIP_ROWS : FLIP_NONE) |
                ((attr & SPR_FLIPY) ? FLIP_PIXELS : FLIP_NONE);
    sprite[active_sprites++] = spr;
  }
}

// One panel line = one native column tx of the 512 x 512 bg plane, walked
// along native y. Tiles are stored rotated (row = 15 - native x).
void commando::blit_bg_line(uint16_t *dst, int tx) {
  const int ty0 = (BG_TY_OFFSET + video.scroll_y) & 511;
  const int col = tx >> 4;
  const int px = tx & 15;
  const uint8_t *codes = video.bg + col * 32; // TILEMAP_SCAN_COLS
  const uint8_t *attrs = codes + 0x400;

  int ty = ty0 & ~15;
  int first = ty0 & 15; // pixels of the first tile above the panel
  for (int u = 0; u < WIDTH; ty += 16) {
    const int trow = (ty >> 4) & 31;
    const uint8_t attr = attrs[trow];
    const uint32_t *tile = tiles[codes[trow] + ((attr & 0xc0) << 2)];
    const unsigned short *colors = cmap_tiles[attr & 15];
    const int gy = (attr & ATTR_FLIPX) ? px : 15 - px;

    uint32_t w[2] = {tile[2 * gy], tile[2 * gy + 1]};
    if (attr & ATTR_FLIPY) {
      const uint32_t w0 = w[0];
      w[0] = rev4bpp(w[1]);
      w[1] = rev4bpp(w0);
    }

    for (int i = first; i < 16 && u < WIDTH; i++, u++) {
      dst[u] = colors[(w[i >> 3] >> ((i & 7) << 2)) & 7];
    }
    first = 0;
  }
}

void commando::blit_bg(short row) {
  for (int l = 0; l < 8; l++) {
    const int x = 255 - (8 * (row - FIRST_ROW) + l); // native visible x
    blit_bg_line(frame_buffer + WIDTH * l, (x + video.scroll_x) & 511);
  }
}

void commando::blit_tile(short row, char col) {
  const uint16_t idx = (col + FG_ROW_OFFSET) * 32 + (FIRST_ROW + 31 - row); // SCAN_ROWS
  const uint8_t attr = memory[FG_ATTR + idx];
  const uint16_t code = memory[FG_CODE + idx] + ((attr & 0xc0) << 2);
  if (blank_chars[code >> 5] & (1u << (code & 31))) {
    return;
  }

  const unsigned short *chr = chars[code];
  const unsigned short *colors = cmap_chars[attr & 15];
  unsigned short *ptr = frame_buffer + 8 * col;
  for (int r = 0; r < 8; r++, ptr += WIDTH - 8) {
    uint32_t pix = chr[(attr & ATTR_FLIPX) ? 7 - r : r];
    if (attr & ATTR_FLIPY) {
      pix = rev2bpp(pix) >> 16;
    }
    for (int c = 0; c < 8; c++, pix >>= 2, ptr++) {
      if ((pix & 3) != PEN_CHAR_CLEAR) {
        *ptr = colors[pix & 3];
      }
    }
  }
}

void commando::blit_sprite(short row, unsigned char s) {
  const sprite_S &spr = sprite[s];
  const unsigned short *colors = cmap_sprites[spr.color];

  // set nibbles = pixels clipped at the panel's left/right edge
  uint32_t mask[2] = {0, 0};
  if (spr.x < 0) {
    const int n = -spr.x;
    mask[0] = n >= 8 ? 0xffffffff : ~(0xffffffff << (4 * n));
    mask[1] = n <= 8 ? 0 : ~(0xffffffff << (4 * (n - 8)));
  }
  if (spr.x > WIDTH - 16) {
    const int n = spr.x - (WIDTH - 16);
    mask[1] = n >= 8 ? 0xffffffff : ~(0xffffffff >> (4 * n));
    mask[0] = n <= 8 ? 0 : ~(0xffffffff >> (4 * (n - 8)));
  }

  const int y_offset = spr.y - 8 * row;
  int lines = y_offset < -8 ? 16 + y_offset : 8;
  int start = 0;
  if (y_offset > 0) {
    start = y_offset;
    lines = 8 - y_offset;
  }

  SpriteRows4bpp rows(sprite_banks[spr.color_block][spr.code], spr.flags);
  if (y_offset < 0) {
    rows.skip(-y_offset);
  }

  unsigned short *ptr = frame_buffer + spr.x + WIDTH * start;
  for (int r = 0; r < lines; r++, ptr += WIDTH - 16) {
    uint32_t w[2];
    rows.next(w);
    for (int h = 0; h < 2; h++) {
      uint32_t pix = w[h] | mask[h];
      for (int c = 0; c < 8; c++, pix >>= 4, ptr++) {
        if ((pix & 15) != PEN_SPRITE_CLEAR) {
          *ptr = colors[pix & 15];
        }
      }
    }
  }
}

void commando::render_row(short row) {
  if (row < FIRST_ROW || row >= LAST_ROW) {
    return;
  }

  blit_bg(row);

  for (unsigned char s = 0; s < active_sprites; s++) {
    if (sprite[s].y < 8 * (row + 1) && sprite[s].y + 16 > 8 * row) {
      blit_sprite(row, s);
    }
  }

  for (char col = 0; col < WIDTH / 8; col++) {
    blit_tile(row, col);
  }
}

void commando::renderSid(int16_t *out, int n) {
  // tune changes run the 6502 init here, on the audio core
  const uint16_t request = music_request;
  if (tune_ok && request != music_seen) {
    music_seen = request;
    const uint8_t subtune = request & 0xff;
    sid_on = subtune != 0;
    if (sid_on) {
      player.start(subtune);
    }
  }

  if (!sid_on) {
    memset(out, 0, n * sizeof(*out));
    return;
  }
  player.render(out, n);
}

#ifdef LED_PIN
// logo colors (quantized logos/commando.png)
static const CRGB SAND = CRGB(0xc8a85a);
static const CRGB BROWN = CRGB(0x87713d);
static const CRGB OLIVE = CRGB(0x5c7a1e);

void commando::menuLeds(CRGB *leds) {
  const CRGB pattern[NUM_LEDS] = {OLIVE, BROWN, SAND, SAND, SAND, BROWN, OLIVE};
  memcpy(leds, pattern, sizeof(pattern));
}

// logo colors, red while the player dies
void commando::gameLeds(CRGB *leds) {
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
