// ============================================================================
// machines/tron/tron.cpp
//
// Tron (Bally Midway 1982), MAME midway/mcr.cpp, mcr_m.cpp, mcr_v.cpp.
// One run_frame() is one 60 Hz field of the 30 Hz interlaced screen: CTC
// trg2 every field, trg3 (the game's frame IRQ) every second field. Main
// and sound CPU are interleaved in 240 slices per field.
// ============================================================================
#ifdef ENABLE_TRON

#include "tron.h"
#include "../../emulation/input.h"
#include "tron_rom.h"
#include "tron_snd_rom.h"
#include "tron_tilemap.h"
#include "tron_spritemap.h"
#include "tron_ssio_prom.h"

static const uint16_t ROM_END = 0xc000;
static const uint16_t NVRAM_END = 0xe000;
static const uint16_t NVRAM_MASK = 0x07ff;
static const uint16_t SPRITE_MASK = 0x01ff;
static const uint16_t VIDEO_MASK = 0x07ff;
static const uint16_t VIDEO_SELECT = 0x0800;  // e800 vs e000 in 0xe000-0xffff
static const uint16_t PALETTE_AREA = 0x0780;  // video RAM offsets 780-7ff

// 19.968 MHz / 8 main, 16 MHz / 8 sound, per 60 Hz field
static const int MAIN_CYCLES = 41600;
static const int SOUND_CYCLES = 33333;
static const int SLICES = 240;

// CTC triggers (mcr_interrupt): trg2 = VBLANK every field, trg3 = 30 Hz frame
static const uint8_t CTC_VBLANK = 2, CTC_FRAME = 3;

// main CPU I/O ports
static const uint8_t PORT_SSIO_STATUS = 0x07;
static const uint8_t PORT_LATCH = 0x1c;  // 1c-1f
static const uint8_t PORT_CTC = 0xf0;    // f0-f3

// SSIO IP0 / IP2, active low
static const uint8_t IP0_COIN = 0x01, IP0_START = 0x04, IP0_FIRE = 0x10;
static const uint8_t IP2_LEFT = 0x01, IP2_RIGHT = 0x02, IP2_UP = 0x04, IP2_DOWN = 0x08;

// game RAM: aim angle 0-35 (c405, see tron_aim.h)
static const uint16_t RAM_AIM_ANGLE = 0x0405;

static const uint8_t LED_FLASH_FRAMES = 4;

static const int PANEL_TOP = 16;  // first panel row of native X 0
static const int SPRITE_CODES = 64, SPRITE_VARIANTS = 4;

tron::tron()
  : tron_rom(tron_rom_blob), tron_snd_rom(tron_snd_rom_blob), tron_tilemap(tron_tilemap_blob),
    tron_spritemap(tron_spritemap_blob) {
  main_rom = tron_rom.data();
  sound_rom = tron_snd_rom.data();
  tiles = tron_tilemap.data();
  sprite_gfx = tron_spritemap.data();

  for (int n = 0; n < SPRITE_CODES; n++) {
    uint32_t any = 0;
    for (int v = 0; v < SPRITE_VARIANTS; v++) {
      for (int r = 0; r < 16; r++) {
        any |= sprite_gfx[v * SPRITE_CODES + n][r][0] | sprite_gfx[v * SPRITE_CODES + n][r][1];
      }
    }
    if (!any) {
      blank_sprites |= (uint64_t)1 << n;
    }
  }
}

void tron::reset() {
  machineBase::reset();
  memset(memory + TRON_MEM_NVRAM, 0xff, NVRAM_MASK + 1);  // NVRAM_DEFAULT_ALL_1
  memset(palette9, 0, sizeof(palette9));
  ctc.reset();
  ssio.attach(sound_rom, memory + TRON_MEM_SSIO, tron_ssio_duty_gain);
  ssio.reset();
  odd_field = false;
  main_debt = 0;
  last_command = 0;
  led_flash = 0;
  aim = TronAim();
}

// ── main Z80 ──

unsigned char IRAM_ATTR tron::opZ80(unsigned short Addr) {
  if (current_cpu == 0 && Addr < ROM_END) {
    return main_rom[Addr];
  }
  return rdZ80(Addr);
}

unsigned char IRAM_ATTR tron::rdZ80(unsigned short Addr) {
  if (current_cpu == 1) {
    return ssio.rd(Addr);
  }
  if (Addr < ROM_END) {
    return main_rom[Addr];
  }
  if (Addr < NVRAM_END) {
    return memory[TRON_MEM_NVRAM + (Addr & NVRAM_MASK)];
  }
  if (Addr & VIDEO_SELECT) {
    return memory[TRON_MEM_VIDEO + (Addr & VIDEO_MASK)];
  }
  return memory[TRON_MEM_SPRITES + (Addr & SPRITE_MASK)];
}

void IRAM_ATTR tron::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == 1) {
    ssio.wr(Addr, Value);
    return;
  }
  if (Addr < ROM_END) {
    return;
  }
  if (Addr < NVRAM_END) {
    memory[TRON_MEM_NVRAM + (Addr & NVRAM_MASK)] = Value;
    return;
  }
  if (!(Addr & VIDEO_SELECT)) {
    memory[TRON_MEM_SPRITES + (Addr & SPRITE_MASK)] = Value;
    return;
  }

  const uint16_t off = Addr & VIDEO_MASK;
  memory[TRON_MEM_VIDEO + off] = Value;

  // the top 0x80 bytes also write the palette: color off / 2, bit 8 = off & 1
  if ((off & PALETTE_AREA) == PALETTE_AREA) {
    palette9[(off >> 1) & (COLORS - 1)] = Value | ((off & 1) << 8);
  }
}

// SSIO input ports 0-4 (mirror 0x18)
uint8_t tron::read_input(uint8_t n) {
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
      v &= ~IP0_FIRE;
    }
    return v;
  case 1:
    return aim.dial();
  case 2:
    // panel up/down = native left/right (ROT90), panel left/right = native down/up
    if (keys & BUTTON_UP) {
      v &= ~IP2_LEFT;
    }
    if (keys & BUTTON_DOWN) {
      v &= ~IP2_RIGHT;
    }
    if (keys & BUTTON_RIGHT) {
      v &= ~IP2_UP;
    }
    if (keys & BUTTON_LEFT) {
      v &= ~IP2_DOWN;
    }
    return v;
  case 3:
    return TRON_IP3;
  case 4:
    return aim.dial();  // player 2 dial (0x0289 reads it for player 2), same controls
  default:
    return 0xff;
  }
}

unsigned char tron::inZ80(unsigned short Port) {
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

// writes 00-07 (SSIO outputs: coin meters, flip, LEDs) and e0/e8 are ignored
void tron::outZ80(unsigned short Port, unsigned char Value) {
  const uint8_t p = Port & 0xff;
  if ((p & 0xfc) == PORT_LATCH) {
    ssio.latch(p & 3, Value);
    if (p == PORT_LATCH && Value != last_command) {
      last_command = Value;
      led_flash = LED_FLASH_FRAMES;
    }
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
void tron::publish_video() {
  vblank.publish([this](VideoState &s) {
    memcpy(s.tiles, memory + TRON_MEM_VIDEO, sizeof(s.tiles));
    memcpy(s.sprites, memory + TRON_MEM_SPRITES, sizeof(s.sprites));
    for (int i = 0; i < COLORS; i++) {
      s.pal[i] = rgb565(palette9[i]);
    }
  });
}

void tron::run_frame(void) {
  ctc.trigger(CTC_VBLANK);
  if (!odd_field) {
    publish_video();
    ctc.trigger(CTC_FRAME);
  }
  odd_field = !odd_field;
  if (led_flash) {
    led_flash--;
  }
  aim.update(input->buttons_get(), memory[TRON_MEM_NVRAM + RAM_AIM_ANGLE]);

  Z80 &z = cpu[0];
  for (int slice = 0; slice < SLICES; slice++) {
    current_cpu = 0;
    main_debt += (slice + 1) * MAIN_CYCLES / SLICES - slice * MAIN_CYCLES / SLICES;
    while (main_debt > 0) {
      if (ctc.irq() && (z.IFF & IFF_1)) {
        IntZ80(&z, ctc.ack());
        game_started = 1;
      }
      const int used = tronStepZ80(z);
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

void tron::prepare_frame(void) {
  vblank.read(video);

  // sprites that can show: half-res position, non-blank code
  active_sprites = 0;
  for (int offs = 0; offs < SPRITE_BYTES; offs += 4) {
    const uint8_t *sr = video.sprites + offs;
    const uint8_t code = sr[1] & 0x3f;
    if ((blank_sprites >> code) & 1) {
      continue;
    }
    sprite_S &s = sprite[active_sprites++];
    s.x = (uint8_t)(sr[2] - 4);    // MAME (sr[2] - 4) * 2 & 0x1ff, halved
    s.y = (uint8_t)(240 - sr[0]);  // MAME (240 - sr[0]) * 2 & 0x1ff, halved
    s.flip_x = (sr[1] >> 6) & 1;
    s.flip_y = (sr[1] >> 7) & 1;
    // sampling variant (romconv): px = hflip, py = !vflip
    s.code = code + SPRITE_CODES * ((!s.flip_y << 1) | s.flip_x);
  }
}

// One strip = native tile column tx (8 native X = 8 panel rows). Tile row
// ty covers panel columns 232 - 8 * ty .. +7; native flipx mirrors the
// strip rows, flipy the columns.
void tron::render_tiles(int tx) {
  for (int ty = 0; ty < TILE_ROWS; ty++) {
    const uint8_t *t = video.tiles + (ty * TILE_COLS + tx) * 2;
    const uint16_t w = t[0] | (t[1] << 8);
    const uint32_t *gfx = tiles[w & 0x1ff];
    const uint16_t *pal = video.pal + ((w >> 11) & 3) * 16;
    const bool flipx = (w >> 9) & 1, flipy = (w >> 10) & 1;
    const int col = OUT_WIDTH - 8 - ty * 8;

    memset(cat + col, ((w >> 14) & 3) << 4, 8);
    for (int r = 0; r < 8; r++) {
      const uint32_t bits = gfx[flipx ? 7 - r : r];
      uint16_t *dst = frame_buffer + r * OUT_WIDTH + col;
      for (int c = 0; c < 8; c++) {
        const int px = flipy ? 7 - c : c;
        dst[c] = pal[(bits >> (28 - 4 * px)) & 15];
      }
    }
  }
}

// MAME ORs the sprite pens together and with the tile's sprite color bank;
// a pixel shows where the low 3 bits are set. Positions wrap at 256.
void tron::render_sprites(int tx) {
  const int x0 = tx * 8;
  bool any = false;
  memset(orbuf, 0, sizeof(orbuf));

  for (int i = 0; i < active_sprites; i++) {
    const sprite_S &s = sprite[i];
    for (int r = 0; r < 8; r++) {
      const int sx = (x0 + r - s.x) & 0xff;  // native x inside the sprite
      if (sx >= 16) {
        continue;
      }
      const uint32_t *bits = sprite_gfx[s.code][s.flip_x ? 15 - sx : sx];
      if (!(bits[0] | bits[1])) {
        continue;
      }
      any = true;
      for (int j = 0; j < 16; j++) {
        const int y = (s.y + j) & 0xff;  // native Y
        if (y >= OUT_WIDTH) {
          continue;
        }
        const int c = s.flip_y ? j : 15 - j;  // data column of native y j
        const uint8_t pen = (bits[c >> 3] >> (28 - 4 * (c & 7))) & 15;
        orbuf[r][OUT_WIDTH - 1 - y] |= pen;
      }
    }
  }
  if (!any) {
    return;
  }

  for (int r = 0; r < 8; r++) {
    uint16_t *dst = frame_buffer + r * OUT_WIDTH;
    for (int c = 0; c < OUT_WIDTH; c++) {
      const uint8_t o = orbuf[r][c];
      if (o & 7) {
        dst[c] = video.pal[cat[c] | o];
      }
    }
  }
}

void tron::render_row(short row) {
  const int tx = row - PANEL_TOP / 8;
  if (tx < 0 || tx >= TILE_COLS) {
    memset(frame_buffer, 0, OUT_WIDTH * 8 * sizeof(uint16_t));
    return;
  }
  render_tiles(tx);
  render_sprites(tx);
}

// ── audio, video core ──

void tron::renderAudio(int16_t *out, int n) {
  ssio.render(out, n);
  audioClampAll(out, n, AUDIO_MAX);
}

#ifdef LED_PIN
// logo colors (quantized logos/tron.png): dark blue, silver, red
static const CRGB TRON_LED_BLUE(0x000049), TRON_LED_SILVER(0xbeb0bc), TRON_LED_RED(0x910021);

void tron::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      TRON_LED_BLUE, TRON_LED_RED, TRON_LED_SILVER, TRON_LED_SILVER,
      TRON_LED_SILVER, TRON_LED_RED, TRON_LED_BLUE};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// red flash on each new sound command (shots, crashes, stage events)
void tron::gameLeds(CRGB *leds) {
  if (led_flash) {
    fill_solid(leds, NUM_LEDS, TRON_LED_RED);
    return;
  }
  menuLeds(leds);
}
#endif

#endif
