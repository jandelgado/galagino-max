#include "gng.h"
#include "gng_rom.h"
#include "gng_bank.h"
#include "gng_sound.h"
#include "gng_chars.h"
#include "gng_tiles.h"
#include "gng_sprites.h"
#include "gng_sid.h"
#include "gng_dipswitches.h"
#include "gng_music.h"
#include "gng_hud.h"
#include "../../emulation/arena.h"
#include "../../emulation/spriteflip.h"

// main 6809: E clock 1.5 MHz, IRQ at vblank. Run in quarters, the sound
// Z80 between them (its IRQ comes 4 x per frame).
static const int MAIN_CYCLES = 1500000 / 60;
static const uint16_t ROM_BASE = 0x4000;   // gng_rom = CPU $4000-$FFFF
static const uint16_t FIXED_BASE = 0x6000; // $6000-$FFFF never banked
static const uint16_t BANK_SIZE = 0x2000;  // $4000-$5FFF
static const uint8_t BANK_FIXED = 4;       // $3E00 = 4: gng_rom $4000

// sound Z80: 3 MHz, IRQ 4 x per frame (MAME 4 * 60 Hz). The IRQ handler
// returns to an "EI; JR $010B" idle loop (MAME trace 2026-10-10).
static const uint16_t SOUND_IRQS = 4;
static const uint16_t SOUND_STEPS = 12500 / 4; // 3 MHz / 240 Hz, >= 4 cycles each
static const uint16_t SOUND_IDLE_PC = 0x010c;

// LS259 at $3D00-$3D07, data bit 0 (MAME mainlatch)
static const uint16_t LATCH_SOUND_RUN = 0x3d01; // low: sound CPU + YM reset

// video: MAME scrolldx 128 / scrolldy 6 against the visible area x 128..,
// y 22.. -> tilemap (x' + scroll x, y' + 16 + scroll y), fg rows + 2
static const int BG_Y_OFFSET = 16;
static const int FG_ROW_OFFSET = 2;
static const int SPRITE_Y_OFFSET = 16;

// bg/fg attribute bits (MAME TILE_FLIPYX((attr & 0x30) >> 4))
static const uint8_t ATTR_FLIPX = 0x10;
static const uint8_t ATTR_FLIPY = 0x20;
static const uint8_t ATTR_FRONT = 0x08; // bg tile group 1

// sprite attribute bits
static const uint8_t SPR_SX_HI = 0x01;
static const uint8_t SPR_FLIPX = 0x04;
static const uint8_t SPR_FLIPY = 0x08;
static const uint8_t SPR_BANK_BLANK = 3; // codes 768-1023: erased ROM

// palette RAM layout (MAME gfxdecode) and transparent pens
static const uint16_t BG_PALETTE = 0x00;     // 8 x 8
static const uint16_t SPRITE_PALETTE = 0x40; // 4 x 16
static const uint16_t FG_PALETTE = 0x80;     // 16 x 4
static const uint8_t PEN_SPRITE_CLEAR = 15;
static const uint8_t PEN_CHAR_CLEAR = 3;
static const uint8_t PEN_BG_BACK0 = 0, PEN_BG_BACK6 = 6; // group 1 behind sprites

static const uint8_t SND_DEATH = 0x31; // sound latch: death jingle, after 3f 00

// main CPU inputs (active low)
static const uint8_t SYS_START1 = 0x01, SYS_COIN1 = 0x40;
static const uint8_t P1_RIGHT = 0x01, P1_LEFT = 0x02, P1_DOWN = 0x04, P1_UP = 0x08;
static const uint8_t P1_FIRE = 0x10, P1_JUMP = 0x20;

// jump on the START button: BUTTON_START with a COIN pin (EXTRA is then
// COIN) or the galagino controller, else START itself reports as
// BUTTON_EXTRA (BUTTON_START only follows a virtual coin, 600 ms late)
#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int JUMP_KEY = BUTTON_START;
#else
static const unsigned int JUMP_KEY = BUTTON_EXTRA;
#endif

gng::gng()
  : gng_rom(gng_rom_blob),
    gng_sprites0(gng_sprites0_blob),
    gng_sprites1(gng_sprites1_blob),
    gng_sprites2(gng_sprites2_blob),
    gng_chars(gng_chars_blob),
    gng_tile_map(gng_tile_map_blob),
    gng_tiles(gng_tiles_blob),
    gng_bank(gng_bank_blob),
    gng_sound(gng_sound_blob),
    gng_sid(gng_sid_blob) {
  // largest first: block A rom + sprites0, B sprites1 + 2 + chars
  rom = gng_rom.data();
  sprite_banks[0] = gng_sprites0.data();
  sprite_banks[1] = gng_sprites1.data();
  sprite_banks[2] = gng_sprites2.data();
  chars = gng_chars.data();
  tile_map = gng_tile_map.data();
  tiles = gng_tiles.data();
  bank_rom = gng_bank.data();
  sound_rom = gng_sound.data();

  // skip fully transparent chars (most of the text layer) per tile
  for (uint16_t c = 0; c < gng_chars.size(); c++) {
    bool blank = true;
    for (int r = 0; r < 8; r++) {
      blank = blank && chars[c][r] == 0xffff;
    }
    if (blank) {
      blank_chars[c >> 5] |= 1u << (c & 31);
    }
  }

  // the tune copies its player from $4000-$4CFF to $C000 and plays there
  tune_ok = psidParse(gng_sid.data(), gng_sid.size(), &tune);
  if (tune_ok) {
    tune_ok = player.load(tune, Arena::alloc<uint8_t>(TUNE_RAM_SIZE), TUNE_RAM_SIZE);
  }
  if (tune_ok) {
    player.alias(0xc000, 0x4000, 0x0d00);
  } else {
    printf("gng: Ghosts_n_Goblins.sid is not a supported PSID\n");
  }
}

void gng::reset() {
  machineBase::reset();
  bank = 0;
  memset(scroll, 0, sizeof(scroll));
  memset(sprite_buffer, 0, sizeof(sprite_buffer));
  sound_latch = 0;
  sound_reset = true;
  memset(ym_addr, 0, sizeof(ym_addr));
  music_song = 0;
  music_request = 0;

  m6809_reset(&main_cpu); // reset vector from $FFFE, read through m6809_read

  // instruction fetches from the fixed ROM skip the callback chain; must
  // follow m6809_reset(), which clears it. Banked code goes through
  // m6809_read_opcode.
  main_cpu.rom_direct = rom + FIXED_BASE - ROM_BASE;
  main_cpu.rom_base = FIXED_BASE;
  main_cpu.rom_size = 0x10000 - FIXED_BASE;
}

unsigned char gng::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

unsigned char gng::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= FIXED_BASE) {
    return rom[addr - ROM_BASE];
  }
  if (addr >= ROM_BASE) {
    if (bank == BANK_FIXED) {
      return rom[addr - ROM_BASE];
    }
    return bank_rom[BANK_SIZE * bank + addr - ROM_BASE];
  }
  if (addr < 0x3000) {
    return memory[MAIN_RAM + addr];
  }

  const unsigned int keys = input->buttons_get();
  switch (addr) {
    case 0x3000: { // SYSTEM
      uint8_t v = 0xff;
      if (keys & BUTTON_START) {
        v &= ~SYS_START1;
      }
      if (keys & BUTTON_COIN) { // level: the game debounces it
        v &= ~SYS_COIN1;
      }
      return v;
    }
    case 0x3001: { // P1; jump on START (one action button + EXTRA)
      uint8_t v = 0xff;
      if (keys & BUTTON_RIGHT) {
        v &= ~P1_RIGHT;
      }
      if (keys & BUTTON_LEFT) {
        v &= ~P1_LEFT;
      }
      if (keys & BUTTON_DOWN) {
        v &= ~P1_DOWN;
      }
      if (keys & BUTTON_UP) {
        v &= ~P1_UP;
      }
      if (keys & (BUTTON_FIRE | BUTTON_A | BUTTON_L1)) {
        v &= ~P1_FIRE;
      }
      if (keys & (JUMP_KEY | BUTTON_B | BUTTON_R1)) {
        v &= ~P1_JUMP;
      }
      return v;
    }
    case 0x3003:
      return GNG_DIP_A;
    case 0x3004:
      return GNG_DIP_B;
  }
  return 0xff;
}

void gng::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr < 0x3000) {
    memory[MAIN_RAM + addr] = val;
    return;
  }
  if (addr >= 0x3800 && addr < 0x3a00) { // palette: RG $38xx, B $39xx
    memory[PALETTE_RAM + addr - 0x3800] = val;
    return;
  }
  if (addr == 0x3a00) {
    wrLatch(val);
    return;
  }
  if (addr >= 0x3b08 && addr <= 0x3b0b) {
    scroll[addr - 0x3b08] = val;
    return;
  }
  if (addr == LATCH_SOUND_RUN) { // other latch bits: flip, coin counters
    const bool reset = !(val & 1);
    if (reset && !sound_reset) {
      ResetZ80(&cpu[0]);
      memset(soundregs, 0, sizeof(soundregs)); // YM2203 reset
    }
    sound_reset = reset;
    return;
  }
  if (addr == 0x3e00) {
    bank = val == BANK_FIXED ? BANK_FIXED : (val & 3);
  }
}

void gng::wrLatch(uint8_t value) {
#ifdef LED_PIN
  if ((value & GNG_CMD_MASK) == SND_DEATH) {
    led_flash = LED_FLASH_FRAMES;
  }
#endif
  // the sound CPU gets every command: FM music is dropped at the YM, SSG
  // effects play. Music changes go to the SID player on the audio core.
  sound_latch = value;

  // first command (00, sound init) follows the boot RAM/ROM test: until
  // then frames run unthrottled (MAME: frame 310 of ~5 s)
  game_started = 1;

  const GngCue cue = gngMusicCue(value);
  if (cue.action == GNG_NONE) {
    return;
  }
  const uint8_t song = cue.action == GNG_PLAY ? cue.song : 0;
  if (song == music_song) {
    return; // a running tune keeps playing
  }
  music_song = song;
  music_request = (uint16_t)((((music_request >> 8) + 1) << 8) | song);
}

unsigned char gng::opZ80(unsigned short Addr) {
  return rdZ80(Addr);
}

unsigned char gng::rdZ80(unsigned short Addr) {
  if (Addr < gng_sound.size()) {
    return sound_rom[Addr];
  }
  if (Addr >= 0xc000 && Addr < 0xc800) {
    return memory[SOUND_RAM + Addr - 0xc000];
  }
  if (Addr == 0xc800) {
    return sound_latch;
  }
  return 0xff;
}

// $E000/$E001 YM2203 #1, $E002/$E003 #2: even = register select, odd =
// data. Only the SSG registers 0-15 are kept (AY layout, 16 per chip).
// The game sets the SSG prescaler to 1/2 (750 kHz AY clock, audio.cpp).
void gng::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0xc000 && Addr < 0xc800) {
    memory[SOUND_RAM + Addr - 0xc000] = Value;
    return;
  }
  if (Addr >= 0xe000 && Addr <= 0xe003) {
    const uint8_t chip = (Addr >> 1) & 1;
    if (!(Addr & 1)) {
      ym_addr[chip] = Value;
    } else if (ym_addr[chip] < 16) {
      soundregs[16 * chip + ym_addr[chip]] = Value;
    }
  }
}

// main CPU for a number of E clock cycles (up to 3 instructions over)
void gng::runMain(int cycles) {
  main_cpu.total_cycles = 0;
  while (main_cpu.total_cycles < cycles) {
    m6809_step(&main_cpu, 4);
  }
}

void gng::runSound() {
  if (sound_reset) {
    return;
  }
  Z80 *z = &cpu[0];
  IntZ80(z, INT_RST38);
  for (int i = 0; i < SOUND_STEPS; i++) {
    if (z->PC.W == SOUND_IDLE_PC && (z->IFF & IFF_1)) {
      break;
    }
    StepZ80(z);
  }
}

void gng::run_frame() {
  for (int q = 0; q < SOUND_IRQS; q++) {
    runMain(MAIN_CYCLES / SOUND_IRQS);
    runSound();
  }

  vblank.publish([this](VideoState &v) {
    memcpy(v.sprites, sprite_buffer, sizeof(v.sprites));
    memcpy(v.bg, memory + BG_RAM, sizeof(v.bg));
    memcpy(v.palette, memory + PALETTE_RAM, sizeof(v.palette));
    v.scroll_x = scroll[0] | (scroll[1] << 8);
    v.scroll_y = scroll[2] | (scroll[3] << 8);
  });
  memcpy(sprite_buffer, memory + SPRITE_RAM, sizeof(sprite_buffer));
  m6809_irq(&main_cpu);
}

// RGBx_444 (R G from $38xx, MAME palette_ext = high byte; B from $39xx)
// -> RGB565, byte-swapped like the other machines
static uint16_t rgb444_to_565(uint8_t rg, uint8_t b) {
  const uint8_t r4 = rg >> 4;
  const uint8_t g4 = rg & 0x0f;
  const uint8_t b4 = b >> 4;
  const uint16_t v = (uint16_t)(((r4 << 1 | r4 >> 3) << 11) | ((g4 << 2 | g4 >> 2) << 5) | (b4 << 1 | b4 >> 3));
  return (uint16_t)((v << 8) | (v >> 8));
}

void gng::prepare_frame() {
  vblank.read(video);

  for (int i = 0; i < 256; i++) {
    colors[i] = rgb444_to_565(video.palette[i], video.palette[0x100 + i]);
  }

  // MAME draw order: offset 1FC first, so lower offsets end up on top
  active_sprites = 0;
  for (int offs = SPRITE_BYTES - 4; offs >= 0; offs -= 4) {
    const uint8_t *s = video.sprites + offs;
    const uint8_t attr = s[1];
    const uint8_t bank = attr >> 6;
    if (bank == SPR_BANK_BLANK) {
      continue;
    }

    sprite_S spr;
    spr.x = s[3] - ((attr & SPR_SX_HI) << 8) - CROP;
    spr.y = 8 * FIRST_ROW + s[2] - SPRITE_Y_OFFSET; // top line of the sprite
    if (spr.x <= -16 || spr.x >= WIDTH || spr.y <= 8 * FIRST_ROW - 16 || spr.y >= 8 * LAST_ROW) {
      continue;
    }
    spr.code = s[0];
    spr.color_block = bank;
    spr.color = (attr >> 4) & 3;
    spr.flags = ((attr & SPR_FLIPX) ? FLIP_PIXELS : FLIP_NONE) |
                ((attr & SPR_FLIPY) ? FLIP_ROWS : FLIP_NONE);
    sprite[active_sprites++] = spr;
  }
}

// One native line y' of the 512 x 512 bg plane, panel columns 0..239.
// Group 1 tiles are also recorded for the front pass.
void gng::blit_bg_line(uint8_t line, int y) {
  uint16_t *dst = frame_buffer + WIDTH * line;
  const int ty = (y + BG_Y_OFFSET + video.scroll_y) & 511;
  const uint8_t trow = ty >> 4;
  const int py = ty & 15;
  const int tx0 = (CROP + video.scroll_x) & 511;

  int tx = tx0 & ~15;
  int first = tx0 & 15; // pixels of the first tile left of the panel
  for (int u = 0; u < WIDTH; tx += 16) {
    const uint16_t idx = ((tx >> 4) & 31) * 32 + trow; // TILEMAP_SCAN_COLS
    const uint8_t attr = video.bg[0x400 + idx];
    const uint32_t *tile = tiles[tile_map[video.bg[idx] + ((attr & 0xc0) << 2)]];
    const int gy = (attr & ATTR_FLIPY) ? 15 - py : py;

    uint32_t w[2] = {tile[2 * gy], tile[2 * gy + 1]};
    if (attr & ATTR_FLIPX) {
      const uint32_t w0 = w[0];
      w[0] = rev4bpp(w[1]);
      w[1] = rev4bpp(w0);
    }

    const uint16_t *pal = colors + BG_PALETTE + 8 * (attr & 7);
    if ((attr & ATTR_FRONT) && front_count < 8 * MAX_FRONT) {
      FrontRun &f = front[front_count++];
      f.w[0] = w[0];
      f.w[1] = w[1];
      f.x = u - first;
      f.line = line;
      f.pal = BG_PALETTE + 8 * (attr & 7);
    }

    for (int i = first; i < 16 && u < WIDTH; i++, u++) {
      dst[u] = pal[(w[i >> 3] >> ((i & 7) << 2)) & 7];
    }
    first = 0;
  }
}

// group 1 bg pixels over the sprites: pens 0 and 6 stay behind (MAME
// transmask split 1: 0x41 back, 0xbe front)
void gng::blit_front(short row) {
  for (uint8_t n = 0; n < front_count; n++) {
    const FrontRun &f = front[n];
    uint16_t *dst = frame_buffer + WIDTH * f.line;
    const uint16_t *pal = colors + f.pal;
    for (int i = 0; i < 16; i++) {
      const int x = f.x + i;
      const uint8_t pen = (f.w[i >> 3] >> ((i & 7) << 2)) & 7;
      if (x < 0 || x >= WIDTH || pen == PEN_BG_BACK0 || pen == PEN_BG_BACK6) {
        continue;
      }
      dst[x] = pal[pen];
    }
  }
}

void gng::blit_tile(short row, char col) {
  static_assert(CROP == 8, "gngFgColumn() crops one fg column");
  const uint16_t idx = (row - FIRST_ROW + FG_ROW_OFFSET) * 32 + gngFgColumn(col, fg_lead); // SCAN_ROWS
  const uint8_t attr = memory[FG_ATTR + idx];
  const uint16_t code = memory[FG_CODE + idx] + ((attr & 0xc0) << 2);
  if (blank_chars[code >> 5] & (1u << (code & 31))) {
    return;
  }

  const unsigned short *chr = chars[code];
  const uint16_t *pal = colors + FG_PALETTE + 4 * (attr & 15);
  unsigned short *ptr = frame_buffer + 8 * col;
  for (int r = 0; r < 8; r++, ptr += WIDTH - 8) {
    uint32_t pix = chr[(attr & ATTR_FLIPY) ? 7 - r : r];
    if (attr & ATTR_FLIPX) {
      pix = rev2bpp(pix) >> 16;
    }
    for (int c = 0; c < 8; c++, pix >>= 2, ptr++) {
      if ((pix & 3) != PEN_CHAR_CLEAR) {
        *ptr = pal[pix & 3];
      }
    }
  }
}

void gng::blit_sprite(short row, unsigned char s) {
  const sprite_S &spr = sprite[s];
  const uint16_t *pal = colors + SPRITE_PALETTE + 16 * spr.color;

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
          *ptr = pal[pix & 15];
        }
      }
    }
  }
}

void gng::render_row(short row) {
  if (row < FIRST_ROW || row >= LAST_ROW) {
    memset(frame_buffer, 0, WIDTH * 8 * sizeof(*frame_buffer));
    return;
  }

  front_count = 0;
  for (uint8_t l = 0; l < 8; l++) {
    blit_bg_line(l, 8 * (row - FIRST_ROW) + l);
  }

  bool sprites_drawn = false;
  for (unsigned char s = 0; s < active_sprites; s++) {
    if (sprite[s].y < 8 * (row + 1) && sprite[s].y + 16 > 8 * row) {
      blit_sprite(row, s);
      sprites_drawn = true;
    }
  }
  if (sprites_drawn) {
    blit_front(row);
  }

  const uint16_t fg_row = (row - FIRST_ROW + FG_ROW_OFFSET) * 32;
  fg_lead = gngLeadRun(memory + FG_CODE + fg_row, memory + FG_ATTR + fg_row, blank_chars);
  for (char col = 0; col < WIDTH / 8; col++) {
    blit_tile(row, col);
  }
}

// tune changes run the 6502 init here, on the audio core
void gng::renderSid(int16_t *out, int n) {
  const uint16_t request = music_request;
  if (tune_ok && request != music_seen) {
    music_seen = request;
    const uint8_t song = request & 0xff;
    sid_on = song != 0;
    if (sid_on) {
      player.start(song);
    }
  }

  if (!sid_on) {
    memset(out, 0, n * sizeof(*out));
    return;
  }
  player.render(out, n);
}

#ifdef LED_PIN
// logo colors (quantized logos/gng.png)
static const CRGB RED = CRGB(0xcc0000);
static const CRGB BLUE = CRGB(0x0055aa);
static const CRGB GREY = CRGB(0xcccccc);

void gng::menuLeds(CRGB *leds) {
  const CRGB pattern[NUM_LEDS] = {RED, BLUE, GREY, GREY, GREY, BLUE, RED};
  memcpy(leds, pattern, sizeof(pattern));
}

// logo colors, red while Arthur dies
void gng::gameLeds(CRGB *leds) {
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
