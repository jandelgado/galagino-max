// ============================================================================
// machines/dotron/dotron.cpp
//
// Discs of Tron (Bally Midway 1983), MAME midway/mcr.cpp, mcr_m.cpp,
// mcr_v.cpp. One run_frame() is one 60 Hz field of the 30 Hz interlaced
// screen: CTC trg2 every field, trg3 (the game's frame IRQ) every second
// field. Main and sound CPU are interleaved in 240 slices per field.
// ============================================================================
#ifdef ENABLE_DOTRON

#include "dotron.h"
#include "../../emulation/input.h"
#include "dotron_rom.h"
#include "dotron_snd_rom.h"
#include "dotron_tilemap.h"
#include "dotron_spritemap.h"
#include "dotron_ssio_prom.h"

// cpu_91490_map
static const uint16_t ROM_END = 0xe000;
static const uint16_t NVRAM_END = 0xe800;
static const uint16_t NVRAM_MASK = 0x07ff;
static const uint16_t SPRITE_END = 0xec00;  // e800-e9ff, mirror 0x200
static const uint16_t SPRITE_MASK = 0x01ff;
static const uint16_t VIDEO_BASE = 0xf000;
static const uint16_t VIDEO_END = 0xf800;   // f800-ffff: palette, write only
static const uint16_t VIDEO_MASK = 0x07ff;
static const uint16_t PALETTE_MASK = 0x007f;
static const uint16_t SOUND_ROM_SIZE = 0x4000;  // SSIO ROM, 4 x 4 KB

// 5 MHz main, 16 MHz / 8 sound, per 60 Hz field
static const int MAIN_CYCLES = 83333;
static const int SOUND_CYCLES = 33333;
static const int SLICES = 240;

// CTC triggers (mcr_interrupt): trg2 = VBLANK every field, trg3 = 30 Hz frame
static const uint8_t CTC_VBLANK = 2, CTC_FRAME = 3;

// main CPU I/O ports
static const uint8_t PORT_SSIO_STATUS = 0x07;
static const uint8_t PORT_LATCH = 0x1c;  // 1c-1f
static const uint8_t PORT_CTC = 0xf0;    // f0-f3

// SSIO IP0 / IP2, active low
static const uint8_t IP0_COIN = 0x01, IP0_START = 0x04, IP0_THROW = 0x10;
static const uint8_t IP2_LEFT = 0x01, IP2_RIGHT = 0x02, IP2_UP = 0x04, IP2_DOWN = 0x08;
static const uint8_t IP2_AIM_DOWN = 0x10, IP2_AIM_UP = 0x20, IP2_DEFLECT = 0x40;
static const uint8_t IP1_DIAL_MASK = 0x7f, IP1_UNUSED = 0x80;
static const uint8_t DIAL_STEP = 4;  // dial counts per frame for L1/R1

// panel layout: native half-res X 8-247 -> column 247 - X, Y -> row Y + 24
static const int X_FIRST = 8, X_LAST = 247;
static const int FIRST_STRIP = 3;  // panel strip of tile row 0

dotron::dotron()
  : dotron_rom(dotron_rom_blob), dotron_snd_rom(dotron_snd_rom_blob),
    dotron_tilemap(dotron_tilemap_blob), dotron_spritemap(dotron_spritemap_blob) {
  main_rom = dotron_rom.data();
  sound_rom = dotron_snd_rom.data();
  tiles = dotron_tilemap.data();
  sprite_gfx = dotron_spritemap.data();

  for (int n = 0; n < SPRITE_CODES; n++) {
    uint32_t any = 0;
    for (int v = 0; v < SPRITE_VARIANTS; v++) {
      for (int r = 0; r < 16; r++) {
        any |= sprite_gfx[v * SPRITE_CODES + n][r][0] | sprite_gfx[v * SPRITE_CODES + n][r][1];
      }
    }
    if (!any) {
      blank_sprites[n >> 6] |= (uint64_t)1 << (n & 63);
    }
  }
}

void dotron::reset() {
  machineBase::reset();
  memset(memory + DOTRON_MEM_NVRAM, 0xff, NVRAM_MASK + 1);  // NVRAM_DEFAULT_ALL_1
  memset(palette9, 0, sizeof(palette9));
  ctc.reset();
  ssio.attach(sound_rom, SOUND_ROM_SIZE, memory + DOTRON_MEM_SSIO, dotron_ssio_duty_gain);
  ssio.reset();
  odd_field = false;
  main_debt = 0;
  dial = 0;
}

// ── main Z80 ──

unsigned char IRAM_ATTR dotron::opZ80(unsigned short Addr) {
  if (current_cpu == 0 && Addr < ROM_END) {
    return main_rom[Addr];
  }
  return rdZ80(Addr);
}

unsigned char IRAM_ATTR dotron::rdZ80(unsigned short Addr) {
  if (current_cpu == 1) {
    return ssio.rd(Addr);
  }
  if (Addr < ROM_END) {
    return main_rom[Addr];
  }
  if (Addr < NVRAM_END) {
    return memory[DOTRON_MEM_NVRAM + (Addr & NVRAM_MASK)];
  }
  if (Addr < SPRITE_END) {
    return memory[DOTRON_MEM_SPRITES + (Addr & SPRITE_MASK)];
  }
  if (Addr >= VIDEO_BASE && Addr < VIDEO_END) {
    return memory[DOTRON_MEM_VIDEO + (Addr & VIDEO_MASK)];
  }
  return 0xff;  // ec00-efff unmapped, palette write only
}

void IRAM_ATTR dotron::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == 1) {
    ssio.wr(Addr, Value);
    return;
  }
  if (Addr < ROM_END) {
    return;
  }
  if (Addr < NVRAM_END) {
    memory[DOTRON_MEM_NVRAM + (Addr & NVRAM_MASK)] = Value;
    return;
  }
  if (Addr < SPRITE_END) {
    memory[DOTRON_MEM_SPRITES + (Addr & SPRITE_MASK)] = Value;
    return;
  }
  if (Addr < VIDEO_BASE) {
    return;
  }
  if (Addr < VIDEO_END) {
    memory[DOTRON_MEM_VIDEO + (Addr & VIDEO_MASK)] = Value;
    return;
  }
  // palette: color off / 2, bit 8 = off & 1 (mcr_paletteram9_w)
  const uint16_t off = Addr & PALETTE_MASK;
  palette9[off >> 1] = Value | ((off & 1) << 8);
}

// SSIO input ports 0-4 (mirror 0x18)
uint8_t dotron::read_input(uint8_t n) {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xff;
  switch (n) {
  case 0:
    if (keys & BUTTON_COIN) {
      v &= ~IP0_COIN;
    }
    if (keys & BUTTON_START) {
      v &= ~IP0_START;
    }
    if (keys & BUTTON_FIRE) {
      v &= ~IP0_THROW;
    }
    return v;
  case 1:
    return IP1_UNUSED | (dial & IP1_DIAL_MASK);
  case 2:
    if (keys & BUTTON_LEFT) {
      v &= ~IP2_LEFT;
    }
    if (keys & BUTTON_RIGHT) {
      v &= ~IP2_RIGHT;
    }
    if (keys & BUTTON_UP) {
      v &= ~IP2_UP;
    }
    if (keys & BUTTON_DOWN) {
      v &= ~IP2_DOWN;
    }
    if (keys & BUTTON_X) {
      v &= ~IP2_AIM_UP;
    }
    if (keys & BUTTON_Y) {
      v &= ~IP2_AIM_DOWN;
    }
    if (keys & BUTTON_B) {
      v &= ~IP2_DEFLECT;
    }
    return v | DOTRON_IP2_UPRIGHT;
  case 3:
    return DOTRON_IP3;
  default:
    return 0xff;
  }
}

unsigned char dotron::inZ80(unsigned short Port) {
  const uint8_t p = Port & 0xff;
  if ((p & 0xf0) == PORT_CTC) {
    return ctc.read(p & 3);
  }
  if (p < 0x20 && (p & 7) <= 4) {
    return read_input(p & 7);
  }
  if (p < 0x20 && (p & 7) == PORT_SSIO_STATUS) {
    return ssio.status();
  }
  return 0xff;
}

// writes 00-07 (SSIO outputs: coin meters, flip, lamps) and e0/e8 are ignored
void dotron::outZ80(unsigned short Port, unsigned char Value) {
  const uint8_t p = Port & 0xff;
  if ((p & 0xfc) == PORT_LATCH) {
    ssio.latch(p & 3, Value);
    return;
  }
  if ((p & 0xfc) == PORT_CTC) {
    ctc.write(p & 3, Value);
  }
}

// ── emulation core ──

static uint16_t rgb565(uint16_t c9) {
  // xRBG 9 bit: R bits 6-8, G bits 0-2, B bits 3-5, each pal3bit
  const uint8_t r3 = (c9 >> 6) & 7, g3 = c9 & 7, b3 = (c9 >> 3) & 7;
  const uint8_t r = (r3 << 5) | (r3 << 2) | (r3 >> 1);
  const uint8_t g = (g3 << 5) | (g3 << 2) | (g3 >> 1);
  const uint8_t b = (b3 << 5) | (b3 << 2) | (b3 >> 1);
  const uint16_t v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  return (uint16_t)((v << 8) | (v >> 8));
}

// Latch what render reads where MAME draws (VIDEO_UPDATE_BEFORE_VBLANK of
// the 30 Hz frame, right before trg3).
void dotron::publish_video() {
  vblank.publish([this](VideoState &s) {
    memcpy(s.tiles, memory + DOTRON_MEM_VIDEO, sizeof(s.tiles));
    memcpy(s.sprites, memory + DOTRON_MEM_SPRITES, sizeof(s.sprites));
    for (int i = 0; i < COLORS; i++) {
      s.pal[i] = rgb565(palette9[i]);
    }
  });
}

void dotron::run_frame(void) {
  ctc.trigger(CTC_VBLANK);
  if (!odd_field) {
    publish_video();
    ctc.trigger(CTC_FRAME);
  }
  odd_field = !odd_field;

  const unsigned int keys = input->buttons_get();
  if (keys & BUTTON_L1) {
    dial += DIAL_STEP;
  }
  if (keys & BUTTON_R1) {
    dial -= DIAL_STEP;
  }

  Z80 &z = cpu[0];
  for (int slice = 0; slice < SLICES; slice++) {
    current_cpu = 0;
    main_debt += (slice + 1) * MAIN_CYCLES / SLICES - slice * MAIN_CYCLES / SLICES;
    while (main_debt > 0) {
      if (ctc.irq() && (z.IFF & IFF_1)) {
        IntZ80(&z, ctc.ack());
        game_started = 1;
      }
      const int used = mcrStepZ80(z);
      main_debt -= used;
      ctc.tick(used);
      if (ctc.zc(0)) {
        ctc.trigger(1);  // ZC/TO0 -> CLK/TRG1
      }
    }

    current_cpu = 1;
    ssio.run(cpu[1], (slice + 1) * SOUND_CYCLES / SLICES - slice * SOUND_CYCLES / SLICES);
  }
  current_cpu = 0;
}

// ── video core ──

// render_sprites_91464: the last RAM entry is on top, so the list starts there
void dotron::prepare_frame(void) {
  vblank.read(video);

  active_sprites = 0;
  for (int offs = SPRITE_BYTES - 4; offs >= 0; offs -= 4) {
    const uint8_t *sr = video.sprites + offs;
    const uint8_t code = (sr[2] + 256 * ((sr[1] >> 3) & 1)) % SPRITE_CODES;
    if ((blank_sprites[code >> 6] >> (code & 63)) & 1) {
      continue;
    }
    sprite_S &s = sprite[active_sprites++];
    s.x = (uint8_t)(sr[3] - 3);    // MAME (sr[3] - 3) * 2 & 0x1ff, halved
    s.y = (uint8_t)(241 - sr[0]);  // MAME (241 - sr[0]) * 2 & 0x1ff, halved
    s.color = (~sr[1] & 3) << 4;
    s.flip_x = (sr[1] >> 4) & 1;
    s.flip_y = (sr[1] >> 5) & 1;
    s.code = code;
  }
}

// One strip = native tile row ty. Panel column = 247 - X, so tile tx covers
// columns 240 - 8 * tx .. +7 (tx 1-30 visible). Stored gfx is mirrored:
// stored pixel c lands on column 240 - 8 * tx + c; native flipx reads
// stored pixel 7 - c, flipy gfx row 7 - r.
void dotron::render_tiles(int ty) {
  for (int tx = 1; tx <= TILE_COLS - 2; tx++) {
    const uint8_t *t = video.tiles + (ty * TILE_COLS + tx) * 2;
    const uint16_t w = t[0] | (t[1] << 8);
    const uint32_t *gfx = tiles[w & 0x1ff];  // 512 tiles, code bit 9 wraps
    const uint16_t *pal = video.pal + ((w >> 12) & 3) * 16;
    const bool flipx = (w >> 10) & 1, flipy = (w >> 11) & 1;
    const int col = OUT_WIDTH - 8 * tx;

    for (int r = 0; r < 8; r++) {
      const uint32_t bits = gfx[flipy ? 7 - r : r];
      uint16_t *dst = frame_buffer + r * OUT_WIDTH + col;
      for (int c = 0; c < 8; c++) {
        const int px = flipx ? 7 - c : c;
        dst[c] = pal[(bits >> (28 - 4 * px)) & 15];
      }
    }
  }
}

// MAME render_sprites_91464: the topmost sprite claims every pixel with a
// non-zero pen, and draws it only if the low 3 bits are set (pen 8 lets the
// tile show through but still hides sprites below). Positions wrap at 256;
// full-res rows 0-1 (half-res Y 0) are clipped.
void dotron::render_sprites(int ty) {
  memset(claim, 0, sizeof(claim));

  for (int i = 0; i < active_sprites; i++) {
    const sprite_S &s = sprite[i];
    const uint16_t *pal = video.pal + s.color;
    // romconv sampling variant: (flip_y << 1 | flip_x) * SPRITE_CODES + code
    const uint32_t (*gfx)[2] = sprite_gfx[((s.flip_y << 1) | s.flip_x) * SPRITE_CODES + s.code];
    for (int r = 0; r < 8; r++) {
      const int y = ty * 8 + r;  // native Y
      const int j = (y - s.y) & 0xff;
      if (j >= 16 || y == 0) {
        continue;
      }
      const uint32_t *bits = gfx[s.flip_y ? 15 - j : j];
      if (!(bits[0] | bits[1])) {
        continue;
      }
      uint16_t *dst = frame_buffer + r * OUT_WIDTH;
      for (int k = 0; k < 16; k++) {
        const int sk = s.flip_x ? 15 - k : k;  // stored (mirrored) pixel
        const uint8_t pen = (bits[sk >> 3] >> (28 - 4 * (sk & 7))) & 15;
        if (!pen) {
          continue;
        }
        const int x = (s.x + 15 - k) & 0xff;  // native X of stored pixel k
        if (x < X_FIRST || x > X_LAST) {
          continue;
        }
        const int c = X_LAST - x;
        if (claim[r][c]) {
          continue;
        }
        claim[r][c] = 1;
        if (pen & 7) {
          dst[c] = pal[pen];
        }
      }
    }
  }
}

void dotron::render_row(short row) {
  const int ty = row - FIRST_STRIP;
  if (ty < 0 || ty >= TILE_ROWS) {
    memset(frame_buffer, 0, OUT_WIDTH * 8 * sizeof(uint16_t));
    return;
  }
  render_tiles(ty);
  render_sprites(ty);
}

// ── audio, video core ──

void dotron::renderAudio(int16_t *out, int n) {
  ssio.render(out, n);
  audioClampAll(out, n, AUDIO_MAX);
}

#endif
