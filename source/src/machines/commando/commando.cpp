#include "commando.h"
#include "commando_rom.h"
#include "commando_chars.h"
#include "commando_tiles.h"
#include "commando_sprites0.h"
#include "commando_sprites1.h"
#include "commando_sprites2.h"
#include "commando_cmap.h"
#include "commando_sid.h"
#include "commando_sound.h"
#include "commando_dipswitches.h"
#include "../../emulation/arena.h"
#include "../../emulation/spriteflip.h"

static const uint16_t ROM_END = 0xc000;
static const uint16_t MAIN_STEPS = INST_PER_FRAME; // per quarter frame, 3 MHz main Z80

// sound Z80: 3 MHz, IRQ 4 x per frame (MAME 4 * 60 Hz). The IRQ handler
// returns to an "EI; JR $-1" idle loop at $0073; stepping it is wasted.
static const uint16_t SOUND_ROM_END = 0x4000;
static const uint16_t SOUND_IRQS = 4;
static const uint16_t SOUND_STEPS = 12500 / 4; // 3 MHz / 240 Hz, >= 4 cycles each
static const uint16_t SOUND_IDLE_PC = 0x0074;
static const uint8_t SOUND_RESET = 0x10; // $C804

// bg/fg attribute bits (MAME TILE_FLIPYX((attr & 0x30) >> 4))
static const uint8_t ATTR_FLIPX = 0x10;
static const uint8_t ATTR_FLIPY = 0x20;

// sprite attribute bits
static const uint8_t SPR_SX_HI = 0x01;
static const uint8_t SPR_FLIPX = 0x04;
static const uint8_t SPR_FLIPY = 0x08;
static const uint8_t SPR_BANK_HIDDEN = 3;

#ifdef COMMANDO_INVINCIBLE
static const uint16_t PLAYER_STATE = 0xe100; // FF alive, 3F..00 dying
static const uint8_t PLAYER_DYING = 0x3f;
#endif

static const uint8_t SND_DEATH = 0x2b; // sound latch: player death jingle
static const uint8_t SND_IDLE = 0xff;  // sound latch: no command

// Commando.sid driver entries (jump table at $5000). PSID subtunes 4-19
// call SID_EFFECT with A = subtune - 4, then SID_STOP.
static const uint16_t SID_STOP = 0x5003;   // music off, effects play on
static const uint16_t SID_EFFECT = 0x500f; // A = effect, over the music
static const uint8_t SID_FIRST_EFFECT = 4;

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
    commando_tiles_a(commando_tiles_a_blob),
    commando_tiles_b(commando_tiles_b_blob),
    commando_sid(commando_sid_blob),
    commando_sound(commando_sound_blob) {
  // this order only: block A sprites0 + tiles_a + cmaps + tune RAM,
  // B sprites2 + tiles_b
  sprite_banks[0] = commando_sprites0.data();
  tiles_a = commando_tiles_a.data();
  sprite_banks[2] = commando_sprites2.data();
  tiles_b = commando_tiles_b.data();
  cmap_tiles = commando_cmap_tiles.data();
  cmap_sprites = commando_cmap_sprites.data();
  cmap_chars = commando_cmap_chars.data();
  sprite_banks[1] = commando_sprites1.data();
  rom = commando_rom.data();
  chars = commando_chars.data();
  sound_rom = commando_sound.data();

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
  memset(sprite_buffer, 0, sizeof(sprite_buffer));
  music_subtune = 0;
  music_request = 0;
  effect_request = 0;
  sound_latch = 0;
  sound_reset = false;
  memset(ym_addr, 0, sizeof(ym_addr));
}

unsigned char commando::opZ80(unsigned short Addr) {
  if (current_cpu) {
    return rdSound(Addr);
  }
  if (Addr >= ROM_END) {
    return rdZ80(Addr);
  }
  return Addr ? decrypt(rom[Addr]) : rom[Addr];
}

unsigned char commando::opPrefixZ80(unsigned short Addr) {
  return opZ80(Addr);
}

unsigned char commando::rdZ80(unsigned short Addr) {
  if (current_cpu) {
    return rdSound(Addr);
  }
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
  if (current_cpu) {
    wrSound(Addr, Value);
    return;
  }
#ifdef COMMANDO_INVINCIBLE
  // every death (bullet $AED9, other hits $925E) starts by setting the
  // player state from FF (alive) to 3F; keep the player alive instead
  if (Addr == PLAYER_STATE && Value == PLAYER_DYING) {
    return;
  }
#endif
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
    // SID effects replace the arcade's SSG effect
    sound_latch = decodeMusic(Value) == MUSIC_EFFECT ? SND_IDLE : Value;
    return;
  }
  if (Addr == 0xc804) { // also coin counters, flip (upright: unused)
    const bool reset = Value & SOUND_RESET;
    if (reset && !sound_reset) {
      ResetZ80(&cpu[1]);
    }
    sound_reset = reset;
    return;
  }
  if (Addr == 0xc806) { // sprite DMA
    memcpy(sprite_buffer, memory + SPRITE_RAM, sizeof(sprite_buffer));
    return;
  }
  if (Addr >= 0xc808 && Addr <= 0xc80b) {
    scroll[Addr - 0xc808] = Value;
  }
}

unsigned char commando::rdSound(unsigned short Addr) {
  if (Addr < SOUND_ROM_END) {
    return sound_rom[Addr];
  }
  if (Addr < 0x4800) {
    return memory[SOUND_RAM + Addr - 0x4000];
  }
  if (Addr == 0x6000) {
    return sound_latch;
  }
  return 0xff;
}

// $8000/$8001 YM2203 #1, $8002/$8003 #2: even = register select, odd =
// data. Only the SSG registers 0-15 are kept (AY layout, 16 per chip).
// The game sets the SSG prescaler to 1/2 (750 kHz AY clock, audio.cpp).
void commando::wrSound(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0x4000 && Addr < 0x4800) {
    memory[SOUND_RAM + Addr - 0x4000] = Value;
    return;
  }
  if (Addr >= 0x8000 && Addr <= 0x8003) {
    const uint8_t chip = (Addr >> 1) & 1;
    if (!(Addr & 1)) {
      ym_addr[chip] = Value;
    } else if (ym_addr[chip] < 16) {
      soundregs[16 * chip + ym_addr[chip]] = Value;
    }
  }
}

void commando::runSound() {
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

// next request for the audio core: sequence + 1, subtune
static uint16_t nextRequest(uint16_t request, uint8_t subtune) {
  return (uint16_t)(((request >> 8) + 1) << 8) | subtune;
}

// sound latch: music commands switch the SID tune (the sound CPU plays
// them on the dropped FM part), cued effects start a SID effect, other
// effects reach the sound CPU
MusicAction commando::decodeMusic(uint8_t command) {
  const MusicCue cue = commandoMusicCue(command);
  if (cue.action == MUSIC_NONE) {
    return cue.action;
  }

  if (cue.action == MUSIC_EFFECT) {
    effect_request = nextRequest(effect_request, cue.subtune);
    return cue.action;
  }

  // a running tune keeps playing (game start sends 20 21 20 21)
  const uint8_t subtune = cue.action == MUSIC_PLAY ? cue.subtune : 0;
  if (subtune == music_subtune) {
    return cue.action;
  }
  music_subtune = subtune;
  music_request = nextRequest(music_request, subtune);
  return cue.action;
}

void commando::run_frame() {
  game_started = 1;

  for (int q = 0; q < SOUND_IRQS; q++) {
    current_cpu = 0;
    for (int i = 0; i < MAIN_STEPS; i++) {
      StepZ80(&cpu[0]);
    }
    runSound();
  }

  vblank.publish([this](VideoState &v) {
    memcpy(v.sprites, sprite_buffer, sizeof(v.sprites));
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
    const uint16_t code = codes[trow] + ((attr & 0xc0) << 2);
    const uint32_t *tile = code < TILES_A ? tiles_a[code] : tiles_b[code - TILES_A];
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
#ifdef COMMANDO_NO_MUSIC
  memset(out, 0, n * sizeof(*out));
  return;
#endif
  // tune changes run the 6502 init here, on the audio core. Stop goes
  // through the driver, so a following effect (death after 2d) plays.
  const uint16_t request = music_request;
  if (tune_ok && request != music_seen) {
    music_seen = request;
    const uint8_t subtune = request & 0xff;
    if (subtune) {
      player.start(subtune);
      sid_on = true;
    } else {
      player.jsr(SID_STOP, 0);
    }
  }

  const uint16_t effect = effect_request;
  if (tune_ok && effect != effect_seen) {
    effect_seen = effect;
    const uint8_t subtune = effect & 0xff;
    if (subtune >= SID_FIRST_EFFECT) { // 0 after reset()
      player.jsr(SID_EFFECT, subtune - SID_FIRST_EFFECT);
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

// C64 SID music plus the AY sound effects, clipped
void commando::renderAudio(int16_t *out, int n) {
  int16_t ssg[AUDIO_CHUNK];
  renderSid(out, n);
  Ay8910::renderBank(ay, soundregs, ssg, n);
  audioAdd(out, ssg, n);
  audioClampAll(out, n, AUDIO_MAX);
}
