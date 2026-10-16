#include "solomon.h"
#include "solomon_main_rom.h"
#include "solomon_audio_rom.h"
#include "solomon_fg_tiles.h"
#include "solomon_bg_tiles.h"
#include "solomon_sprites.h"

using namespace solomon_video;

// Frame budget, pbaction (same board family) as reference: StepZ80 calls per
// INST_PER_FRAME pass. 4 MHz ~ 66667 cycles ~ 8000 instr, 3.072 MHz ~ 5000.
#ifndef SOLOMON_MAIN_STEPS
#define SOLOMON_MAIN_STEPS 8
#endif
#ifndef SOLOMON_AUDIO_STEPS
#define SOLOMON_AUDIO_STEPS 4
#endif

static const unsigned short ROM_END = 0xc000;
static const unsigned short ROM_HIGH = 0xf000;    // stored at image offset ROM_END
static const unsigned short RAM_END = 0xe000;     // c000-dfff work + video RAM
static const unsigned short SPRITE_ADDR = 0xe000, SPRITE_END = 0xe080;
static const unsigned short PALETTE_ADDR = 0xe400, PALETTE_END = 0xe600;
static const unsigned short AUDIO_ROM_END = 0x4000;
static const unsigned short AUDIO_RAM = 0x4000, AUDIO_RAM_END = 0x4800;
static const unsigned short AUDIO_LATCH = 0x8000;
static const unsigned short E603_PC_SCORE = 0x4cf0; // MAME: return BC & 0x08
static const uint8_t AUDIO_IRQS = 2;                // 120 Hz

static const uint8_t P1_RIGHT = 0x01, P1_LEFT = 0x02, P1_UP = 0x04, P1_DOWN = 0x08;
static const uint8_t P1_FIREBALL = 0x10, P1_WAND = 0x20;
static const uint8_t SYS_START1 = 0x01, SYS_COIN1 = 0x04;

// sprite_S::flags (sprite_S::code is 8 bit, as gberet)
static const uint8_t SPR_FLIPX = 0x01;
static const uint8_t SPR_FLIPY = 0x02;
static const uint8_t SPR_HI = 0x04;  // code bit 8

// fireball (BUTTON2) on START, as gng's jump: BUTTON_START with a COIN pin or
// the galagino controller, else START itself reports as BUTTON_EXTRA
#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int FIREBALL_KEY = BUTTON_START;
#else
static const unsigned int FIREBALL_KEY = BUTTON_EXTRA;
#endif

solomon::solomon()
  : solomon_main_rom(solomon_main_rom_blob),
    solomon_audio_rom(solomon_audio_rom_blob),
    solomon_fg_tiles(solomon_fg_tiles_blob),
    solomon_bg_tiles(solomon_bg_tiles_blob),
    solomon_sprites(solomon_sprites_blob) {
  // Unpack eagerly: bg -> block A, fg -> block B, audio -> A
  bg_tiles = solomon_bg_tiles.data();
  fg_tiles = solomon_fg_tiles.data();
  audio_rom = solomon_audio_rom.data();
  main_rom = solomon_main_rom.data();
  sprites = solomon_sprites.data();

#ifdef SOLOMON_SCALE
  for (int c = 0; c < OUT_WIDTH; c++) {
    x_map[c] = scaleX(c);
  }
#endif
}

void solomon::reset() {
  machineBase::reset();
  nmi_mask = false;
  audio_irq = false;
  audio_nmi = false;
  sound_latch = 0;
  memset(ay_address, 0, sizeof(ay_address));
  memset(palette, 0, sizeof(palette));
  scroll_x = CROP_LEFT;
}

// ── CPU access ──

unsigned char solomon::opZ80(unsigned short Addr) {
  if (current_cpu == 1) {
    return rdSound(Addr);
  }
  return rdMain(Addr);
}

unsigned char solomon::rdZ80(unsigned short Addr) {
  return current_cpu == 1 ? rdSound(Addr) : rdMain(Addr);
}

unsigned char solomon::rdSound(unsigned short Addr) {
  if (Addr < AUDIO_ROM_END) {
    return audio_rom[Addr];
  }
  if (Addr >= AUDIO_RAM && Addr < AUDIO_RAM_END) {
    return memory[SOL_AUDIO_RAM + (Addr - AUDIO_RAM)];
  }
  if (Addr == AUDIO_LATCH) {
    return sound_latch;
  }
  return 0xff;
}

unsigned char solomon::rdMain(unsigned short Addr) {
  if (Addr < ROM_END) {
    return main_rom[Addr];
  }
  if (Addr >= ROM_HIGH) {
    return main_rom[ROM_END + (Addr - ROM_HIGH)];
  }
  if (Addr < RAM_END) {
    return memory[SOL_WORK_RAM + (Addr - ROM_END)];
  }
  if (Addr < SPRITE_END) {
    return memory[SOL_SPRITE_RAM + (Addr - SPRITE_ADDR)];
  }
  if (Addr >= PALETTE_ADDR && Addr < PALETTE_END) {
    return memory[SOL_PALETTE_RAM + (Addr - PALETTE_ADDR)];
  }

  switch (Addr) {
    case 0xe600: return readP1();
    case 0xe601: return 0x00;  // P2, cocktail only
    case 0xe602: return readSystem();
    case 0xe603: return readE603();
    case 0xe604: return SOLOMON_DSW1 | (input->demoSoundsOff() ? SOLOMON_DSW1_DEMO_SOUND_OFF : 0);
    case 0xe605: return SOLOMON_DSW2;
    default: return 0xff;  // e606 watchdog, unmapped
  }
}

unsigned char solomon::readP1(void) {
  const unsigned int k = input->buttons_get();
  unsigned char v = 0;
  if (k & BUTTON_RIGHT) {
    v |= P1_RIGHT;
  }
  if (k & BUTTON_LEFT) {
    v |= P1_LEFT;
  }
  if (k & BUTTON_UP) {
    v |= P1_UP;
  }
  if (k & BUTTON_DOWN) {
    v |= P1_DOWN;
  }
  if (k & (BUTTON_FIRE | BUTTON_A)) {
    v |= P1_WAND;
  }
  if (k & (FIREBALL_KEY | BUTTON_B)) {
    v |= P1_FIREBALL;
  }
  return v;
}

unsigned char solomon::readSystem(void) {
  const unsigned int k = input->buttons_get();
  unsigned char v = 0;
  if (k & BUTTON_START) {
    v |= SYS_START1;
  }
  if (k & BUTTON_COIN) {  // level, as gng
    v |= SYS_COIN1;
  }
  return v;
}

// Unknown board logic, MAME keys it on the reading instruction: 0 at
// PC 0x161 (coin/start) and everywhere else, BC & 0x08 at the score check.
// The Z80 core has fetched the operands, so PC is past `ld a,(e603)` as in MAME.
unsigned char solomon::readE603(void) {
  if (cpu[0].PC.W == E603_PC_SCORE) {
    return cpu[0].BC.W & 0x08;
  }
  return 0;
}

void solomon::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == 0) {
    wrMain(Addr, Value);
    return;
  }
  if (Addr >= AUDIO_RAM && Addr < AUDIO_RAM_END) {
    memory[SOL_AUDIO_RAM + (Addr - AUDIO_RAM)] = Value;
  }
  // ffff: watchdog, ignored
}

void solomon::wrMain(unsigned short Addr, unsigned char Value) {
  if (Addr >= ROM_END && Addr < RAM_END) {
    memory[SOL_WORK_RAM + (Addr - ROM_END)] = Value;
    return;
  }
  if (Addr >= SPRITE_ADDR && Addr < SPRITE_END) {
    memory[SOL_SPRITE_RAM + (Addr - SPRITE_ADDR)] = Value;
    return;
  }
  if (Addr >= PALETTE_ADDR && Addr < PALETTE_END) {
    writePalette(Addr - PALETTE_ADDR, Value);
    return;
  }

  switch (Addr) {
    case 0xe600:
      nmi_mask = Value & 0x01;
      if (nmi_mask) {
        game_started = 1;  // ROM enables the NMI once its RAM test is done
      }
      return;
    case 0xe800:
      sound_latch = Value;
      audio_nmi = true;
      return;
    default:
      return;  // e604 flip screen: upright only
  }
}

void solomon::writePalette(unsigned short offset, unsigned char value) {
  memory[SOL_PALETTE_RAM + offset] = value;
  const unsigned short e = offset & ~1;
  palette[offset >> 1] = paletteColor(memory[SOL_PALETTE_RAM + e], memory[SOL_PALETTE_RAM + e + 1]);
}

// sound ports 0x10/0x20/0x30: YM2149 address (even) / data (odd)
void solomon::outZ80(unsigned short Port, unsigned char Value) {
  if (current_cpu != 1) {
    return;
  }
  const unsigned char p = Port & 0xff;
  const int chip = (p >> 4) - 1;
  if (chip < 0 || chip > 2) {
    return;
  }
  if (p & 1) {
    soundregs[chip * 16 + ay_address[chip]] = Value;
  } else {
    ay_address[chip] = Value & 0x0f;
  }
}

unsigned char solomon::inZ80(unsigned short Port) {
  return 0xff;  // no readable ports
}

// ── frame ──

void solomon::run_frame(void) {
  const int irq_period = INST_PER_FRAME / AUDIO_IRQS;

  for (int i = 0; i < INST_PER_FRAME; i++) {
    if (i % irq_period == 0) {
      audio_irq = true;
    }

    current_cpu = 0;
    for (int s = 0; s < SOLOMON_MAIN_STEPS; s++) {
      StepZ80(&cpu[0]);
    }

    current_cpu = 1;
    if (audio_nmi) {
      audio_nmi = false;
      IntZ80(&cpu[1], INT_NMI);
    }
    // irq0_line_hold: held until the CPU takes it
    if (audio_irq && (cpu[1].IFF & IFF_1)) {
      audio_irq = false;
      IntZ80(&cpu[1], INT_IRQ);
    }
    for (int s = 0; s < SOLOMON_AUDIO_STEPS; s++) {
      StepZ80(&cpu[1]);
    }
  }

  publishVideo();
  current_cpu = 0;
  if (nmi_mask) {
    IntZ80(&cpu[0], INT_NMI);
  }
}

void solomon::publishVideo(void) {
  vblank.publish([this](VideoState &s) {
    const int vis = FIRST_TILE_ROW * TILE_COLS;
    memcpy(s.fg_color, memory + SOL_FG_COLOR + vis, sizeof(s.fg_color));
    memcpy(s.fg_video, memory + SOL_FG_VIDEO + vis, sizeof(s.fg_video));
    memcpy(s.bg_color, memory + SOL_BG_COLOR + vis, sizeof(s.bg_color));
    memcpy(s.bg_video, memory + SOL_BG_VIDEO + vis, sizeof(s.bg_video));
    memcpy(s.spr, memory + SOL_SPRITE_RAM, sizeof(s.spr));
  });
}

void solomon::prepare_frame(void) {
  vblank.read(video);
  prepare_frame_sprites();
#ifdef SOLOMON_SCROLL
  followPlayer();
#endif
}

// 1 px per frame toward Dana's target, so a respawn or new room does not
// jump; centered while she is not on screen (title, room intro)
void solomon::followPlayer(void) {
  const uint8_t *s = video.spr + PLAYER_SPRITE;
  const int y = spriteLine(s[2]);
  const bool visible = y > -SPRITE_SIZE && y < VIS_LINES && spriteColor(s[1]) == PLAYER_COLOR;
  const uint8_t target = visible ? scrollFor(s[3]) : CROP_LEFT;

  if (scroll_x < target) {
    scroll_x++;
  } else if (scroll_x > target) {
    scroll_x--;
  }
}

// MAME order: offs 0x7c first, offs 0 drawn last (on top)
void solomon::prepare_frame_sprites(void) {
  active_sprites = 0;
  for (int offs = SPRITE_BYTES - 4; offs >= 0; offs -= 4) {
    const uint8_t *s = video.spr + offs;
    const int y = spriteLine(s[2]);
    if (y <= -SPRITE_SIZE || y >= VIS_LINES) {
      continue;
    }
    sprite_S &spr = sprite[active_sprites++];
    const uint16_t code = spriteCode(s[0], s[1]);
    spr.x = s[3];
    spr.y = y;
    spr.code = code & 0xff;
    spr.color = spriteColor(s[1]);
    spr.flags = ((s[1] & 0x40) ? SPR_FLIPX : 0) | ((s[1] & 0x80) ? SPR_FLIPY : 0) |
                ((code & 0x100) ? SPR_HI : 0);
  }
}

// one 8 px tile row, pixel 0 in bits 31-28
static inline uint8_t pen(uint32_t bits, int px) { return (bits >> ((7 - px) * 4)) & 15; }

// bg opaque, fg and sprites pen 0 transparent, all in native x; then
// outputLine picks the 240 output columns
void solomon::render_line(uint16_t *dst, int line) {
  uint16_t native[NATIVE_WIDTH];
  const int trow = line >> 3;
  const int ty = line & 7;

  for (int col = 0; col < TILE_COLS; col++) {
    const int i = trow * TILE_COLS + col;
    const uint8_t attr = video.bg_color[i];
    const int y = (attr & 0x08) ? 7 - ty : ty;
    const uint32_t bits = bg_tiles[tileCode(video.bg_video[i], attr)][y];
    const uint16_t *cmap = palette + BG_PALETTE + 16 * tileColor(attr);
    uint16_t *p = native + col * 8;
    for (int px = 0; px < 8; px++) {
      p[px] = cmap[pen(bits, (attr & 0x80) ? 7 - px : px)];
    }
  }

  for (int col = 0; col < TILE_COLS; col++) {
    const int i = trow * TILE_COLS + col;
    const uint8_t attr = video.fg_color[i];
    const uint32_t bits = fg_tiles[tileCode(video.fg_video[i], attr)][ty];
    if (!bits) {
      continue;
    }
    const uint16_t *cmap = palette + 16 * tileColor(attr);
    uint16_t *p = native + col * 8;
    for (int px = 0; px < 8; px++) {
      const uint8_t c = pen(bits, px);
      if (c) {
        p[px] = cmap[c];
      }
    }
  }

  for (int n = 0; n < active_sprites; n++) {
    const sprite_S &s = sprite[n];
    int r = line - s.y;
    if (r < 0 || r >= SPRITE_SIZE) {
      continue;
    }
    if (s.flags & SPR_FLIPY) {
      r = SPRITE_SIZE - 1 - r;
    }
    const uint32_t *bits = sprites[s.code | ((s.flags & SPR_HI) << 6)][r];
    if (!(bits[0] | bits[1])) {
      continue;
    }
    const uint16_t *cmap = palette + 16 * s.color;
    for (int c = 0; c < SPRITE_SIZE; c++) {
      const int x = s.x + c;
      if (x >= NATIVE_WIDTH) {
        break;  // clipped, no wrap
      }
      const int px = (s.flags & SPR_FLIPX) ? SPRITE_SIZE - 1 - c : c;
      const uint8_t p = pen(bits[px >> 3], px & 7);
      if (p) {
        native[x] = cmap[p];
      }
    }
  }

  outputLine(dst, native, line);
}

// scale: fixed column map; crop/scroll: 240 px window at scroll_x, HUD
// lines fixed with their empty columns dropped
void solomon::outputLine(uint16_t *dst, const uint16_t *native, int line) {
#ifdef SOLOMON_SCALE
  for (int c = 0; c < OUT_WIDTH; c++) {
    dst[c] = native[x_map[c]];
  }
#else
  if (line < HUD_TOP_LINES) {
    for (int c = 0; c < OUT_WIDTH; c++) {
      dst[c] = native[hudTopX(c)];
    }
    return;
  }

  if (line >= HUD_BOTTOM) {
    for (int c = 0; c < OUT_WIDTH; c++) {
      dst[c] = native[hudBottomX(c)];
    }
    return;
  }

  memcpy(dst, native + scroll_x, OUT_WIDTH * sizeof(uint16_t));
#endif
}

void solomon::render_row(short row) {
  for (int sr = 0; sr < 8; sr++) {
    uint16_t *dst = frame_buffer + sr * OUT_WIDTH;
    const int line = row * 8 + sr - FIRST_LINE;
    if (line < 0 || line >= VIS_LINES) {
      memset(dst, 0, OUT_WIDTH * sizeof(uint16_t));
      continue;
    }
    render_line(dst, line);
  }
}

#ifdef LED_PIN
// logo colors (quantized logos/solomon.png): scroll, stone, KEY green, title red
static const CRGB SOLOMON_LED_SCROLL(0xcc7755), SOLOMON_LED_STONE(0x3d4f60),
                  SOLOMON_LED_GREEN(0x9dd2a0), SOLOMON_LED_RED(0xc43543);
static const CRGB SOLOMON_LED_FIRE(0xff6000);  // fireball
static const uint8_t FIRE_BLINK = 4;           // frames per blink phase

void solomon::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      SOLOMON_LED_STONE, SOLOMON_LED_SCROLL, SOLOMON_LED_RED, SOLOMON_LED_GREEN,
      SOLOMON_LED_RED, SOLOMON_LED_SCROLL, SOLOMON_LED_STONE};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// blink orange while the fireball key is held, else the menu colors
void solomon::gameLeds(CRGB *leds) {
  if (!(input->buttons_get() & (FIREBALL_KEY | BUTTON_B))) {
    menuLeds(leds);
    return;
  }
  led_frame++;
  fill_solid(leds, NUM_LEDS, (led_frame & FIRE_BLINK) ? SOLOMON_LED_FIRE : CRGB::Black);
}
#endif
