#include "thepit.h"
#include "thepit_logo.h"
#include "thepit_main_rom.h"
#include "thepit_sound_rom.h"
#include "thepit_tiles.h"
#include "thepit_sprites.h"
#include "thepit_cmap.h"
#include "../../emulation/spriteflip.h"

using namespace thepit_video;

thepit::thepit()
  : thepit_main_rom(thepit_main_rom_blob),
    thepit_sound_rom(thepit_sound_rom_blob),
    thepit_tiles(thepit_tiles_blob),
    thepit_sprites(thepit_sprites_blob),
    thepit_cmap(thepit_cmap_blob) {
  main_rom = thepit_main_rom.data();
  sound_rom = thepit_sound_rom.data();
  tiles = thepit_tiles.data();
  sprites = thepit_sprites.data();
  colors = thepit_cmap.data()[0];
}

// Runs before start(): touches own scalars only.
void thepit::reset() {
  machineBase::reset();
  sound_latch = 0;
  ay_addr = 0;
  snd_irq_pending = 0;
  sound_enabled = 0;
}

// Runs twice: assignments only.
void thepit::start() {
  ram = memory + MAIN_RAM_OFFSET;
  cram = memory + COLOR_RAM_OFFSET;
  vram = memory + VIDEO_RAM_OFFSET;
  objram = memory + OBJRAM_OFFSET;
  snd_ram = memory + SND_RAM_OFFSET;
}

// 0x9800-0x98ff object RAM; attribute bytes 0x00-0x3f also mirror at
// 0x99xx-0x9fxx. nullptr: unmapped.
uint8_t *thepit::objram_at(uint16_t addr) {
  uint8_t off = addr & 0xff;
  if (addr < OBJRAM_ADDR + OBJRAM_SIZE || off < ATTR_SIZE) {
    return objram + off;
  }
  return nullptr;
}

uint8_t thepit::in0() {
  unsigned int keys = input->buttons_get();
  uint8_t v = 0;

  if (keys & BUTTON_LEFT)  { v |= IN0_LEFT; }
  if (keys & BUTTON_RIGHT) { v |= IN0_RIGHT; }
  if (keys & BUTTON_DOWN)  { v |= IN0_DOWN; }
  if (keys & BUTTON_UP)    { v |= IN0_UP; }
  if (keys & BUTTON_FIRE)  { v |= IN0_FIRE; }
  return v;
}

uint8_t thepit::in1() {
  unsigned int keys = input->buttons_get();
  uint8_t v = 0;

  if (keys & BUTTON_COIN)  { v |= IN1_COIN; }
  if (keys & BUTTON_START) { v |= IN1_START1; }
  return v;
}

unsigned char thepit::opZ80(unsigned short Addr) {
  if (current_cpu == MAIN_CPU && Addr < MAIN_ROM_SIZE) {
    return main_rom[Addr];
  }
  if (current_cpu == SOUND_CPU && Addr < SND_ROM_SIZE) {
    return sound_rom[Addr];
  }
  return rdZ80(Addr);
}

unsigned char thepit::rdZ80(unsigned short Addr) {
  if (current_cpu == SOUND_CPU) {
    if (Addr < SND_ROM_SIZE) {
      return sound_rom[Addr];
    }
    if (Addr >= SND_RAM_ADDR && Addr < SND_RAM_ADDR + SND_RAM_SIZE) {
      return snd_ram[Addr - SND_RAM_ADDR];
    }
    return 0xff;
  }

  if (Addr < MAIN_ROM_SIZE) {
    return main_rom[Addr];
  }
  if (Addr >= RAM_ADDR && Addr < RAM_ADDR + RAM_SIZE) {
    return ram[Addr - RAM_ADDR];
  }
  if ((Addr & 0xf800) == CRAM_ADDR) {
    return cram[Addr & (CRAM_SIZE - 1)];
  }
  if ((Addr & 0xf800) == VRAM_ADDR) {
    return vram[Addr & (VRAM_SIZE - 1)];
  }
  if (Addr >= OBJRAM_ADDR && Addr < OBJRAM_ADDR + OBJ_WINDOW) {
    uint8_t *p = objram_at(Addr);
    return p ? *p : 0xff;
  }

  switch (Addr) {
    case IN0_ADDR:      return in0();
    case IN1_ADDR:      return in1();
    case DSW_ADDR:      return THEPIT_DSW_VALUE;
    case WATCHDOG_ADDR: return 0xff;
  }
  return 0xff;
}

// LS259 at 0xb000-0xb007: bit = address & 7, value = D0
void thepit::latch_w(uint8_t bit, uint8_t on) {
  if (bit == LATCH_NMI_ENABLE) {
    irq_enable[MAIN_CPU] = on;
    if (on) {
      game_started = 1;
    }
    return;
  }

  // MAME mutes all sound while disabled: silence the AY volumes
  if (bit == LATCH_SOUND_ENABLE) {
    sound_enabled = on;
    if (!on) {
      memset(soundregs + AY_VOLUME_A, 0, AY_VOLUME_C - AY_VOLUME_A + 1);
    }
  }
  // 2 coin lockout (unused), 6/7 flip x/y: cocktail only, ignored
}

void thepit::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == SOUND_CPU) {
    if (Addr >= SND_RAM_ADDR && Addr < SND_RAM_ADDR + SND_RAM_SIZE) {
      snd_ram[Addr - SND_RAM_ADDR] = Value;
    }
    return;
  }

  if (Addr >= RAM_ADDR && Addr < RAM_ADDR + RAM_SIZE) {
    ram[Addr - RAM_ADDR] = Value;
    return;
  }
  if ((Addr & 0xf800) == CRAM_ADDR) {
    cram[Addr & (CRAM_SIZE - 1)] = Value;
    return;
  }
  if ((Addr & 0xf800) == VRAM_ADDR) {
    vram[Addr & (VRAM_SIZE - 1)] = Value;
    return;
  }
  if (Addr >= OBJRAM_ADDR && Addr < OBJRAM_ADDR + OBJ_WINDOW) {
    uint8_t *p = objram_at(Addr);
    if (p) {
      *p = Value;
    }
    return;
  }
  if ((Addr & 0xfff8) == LATCH_ADDR) {
    latch_w(Addr & 7, Value & 1);
    return;
  }
  if (Addr == SNDLATCH_ADDR) {
    sound_latch = Value;
  }
}

unsigned char thepit::inZ80(unsigned short Port) {
  if ((Port & 0xff) != AY_DATA_PORT) {
    return 0xff;
  }
  if (ay_addr == AY_PORT_A) {
    return sound_latch;
  }
  return ay_addr < AY_PORT_A ? soundregs[ay_addr] : 0xff;
}

void thepit::outZ80(unsigned short Port, unsigned char Value) {
  switch (Port & 0xff) {
    case SND_CLEAR_PORT:
      sound_latch = 0;
      return;
    case AY_ADDR_PORT:
      ay_addr = Value & 0x0f;
      return;
    case AY_DATA_PORT:
      if (ay_addr >= AY_PORT_A) {
        return;
      }
      // sound disabled: keep the channel volumes silent
      if (!sound_enabled && ay_addr >= AY_VOLUME_A && ay_addr <= AY_VOLUME_C) {
        Value = 0;
      }
      soundregs[ay_addr] = Value;
      return;
  }
}

void thepit::run_frame(void) {
  // Main Z80 @ 3.072 MHz, sound Z80 @ 2.5 MHz: 4 : 3 instructions
  for (int i = 0; i < INST_PER_FRAME; i++) {
    current_cpu = MAIN_CPU;
    StepZ80(&cpu[MAIN_CPU]); StepZ80(&cpu[MAIN_CPU]);
    StepZ80(&cpu[MAIN_CPU]); StepZ80(&cpu[MAIN_CPU]);

    current_cpu = SOUND_CPU;
    StepZ80(&cpu[SOUND_CPU]); StepZ80(&cpu[SOUND_CPU]); StepZ80(&cpu[SOUND_CPU]);

    // MAME holds the vsync IRQ until acknowledged: deliver once enabled
    if (snd_irq_pending && (cpu[SOUND_CPU].IFF & IFF_1)) {
      IntZ80(&cpu[SOUND_CPU], INT_RST38);
      snd_irq_pending = 0;
    }
  }

  vblank.publish([this](VideoState &v) {
    memcpy(v.vram, vram, sizeof(v.vram));
    memcpy(v.cram, cram, sizeof(v.cram));
    memcpy(v.attr, objram, sizeof(v.attr));
    memcpy(v.sprite, objram + SPRITE_OFFSET, sizeof(v.sprite));
  });

  if (irq_enable[MAIN_CPU]) {
    current_cpu = MAIN_CPU;
    IntZ80(&cpu[MAIN_CPU], INT_NMI);
  }
  snd_irq_pending = 1;
}

void thepit::prepare_frame(void) {
  vblank.read(video);
  active_sprites = 0;

  // MAME draws all low priority sprites, then all high ones; within a
  // pass sprite 7 first, so sprite 0 ends on top.
  for (uint8_t high = 0; high < 2; high++) {
    if (high) {
      first_high = active_sprites;
    }

    for (int idx = SPRITE_COUNT - 1; idx >= 0; idx--) {
      Sprite s;
      if (!decodeSprite(video.sprite + 4 * idx, idx, s) || s.high != high) {
        continue;
      }

      sprite_S &d = sprite[active_sprites++];
      d.x = s.x;
      d.y = s.y;
      d.code = s.code;
      d.color = s.color;
      d.flags = s.flip;
    }
  }
}

// Chars over solid background for all tile slots of the strip. Priority
// tiles end up solid in MAME (the solid layer redraws over the chars), so
// they skip the char decode; their spans are returned for the refill over
// low priority sprites.
uint8_t thepit::draw_tiles(short row, Span *prio) {
  uint8_t scroll = video.attr[2 * (row - FIRST_ROW)];
  uint8_t sub = scroll & 7;
  uint8_t count = 0;

  for (int slot = sub ? -1 : 0; slot < TILE_SLOTS - 1; slot++) {
    int x0 = 8 * slot + sub;
    int first = x0 < 0 ? -x0 : 0;
    int last = x0 + 8 > WIDTH ? WIDTH - x0 : 8;

    uint16_t addr = tileAddr(row, slot, scroll);
    uint8_t c = video.cram[addr];
    uint16_t bg = colors[BG_COLOR_BASE + bgColor(c)];

    if (isPriority(c)) {
      prio[count] = { (int16_t)(x0 + first), (int16_t)(x0 + last), bg };
      fill_span(prio[count++]);
      continue;
    }

    const unsigned short *pal = colors + 4 * charColor(c);
    const unsigned short *tile = tiles[video.vram[addr]];
    unsigned short *ptr = frame_buffer + x0;

    for (int r = 0; r < 8; r++, ptr += WIDTH) {
      unsigned short pix = tile[r] >> (2 * first);
      for (int px = first; px < last; px++, pix >>= 2) {
        uint8_t pen = pix & 3;
        ptr[px] = pen ? pal[pen] : bg;
      }
    }
  }
  return count;
}

void thepit::fill_span(const Span &span) {
  unsigned short *ptr = frame_buffer;
  for (int r = 0; r < 8; r++, ptr += WIDTH) {
    for (int x = span.x0; x < span.x1; x++) {
      ptr[x] = span.color;
    }
  }
}

// Draws sprite[from..to) overlapping the strip, returns how many.
uint8_t thepit::draw_sprites(short row, int from, int to) {
  uint8_t drawn = 0;
  for (int s = from; s < to; s++) {
    if (sprite[s].y >= 8 * (row + 1) || sprite[s].y + 16 <= 8 * row) {
      continue;
    }
    blit_sprite(row, s);
    drawn++;
  }
  return drawn;
}

void thepit::blit_sprite(short row, unsigned char s) {
  const sprite_S &spr = sprite[s];
  SpriteRows2bpp rows(sprites[spr.code], spr.flags);
  const unsigned short *pal = colors + 4 * spr.color;

  // columns outside the screen
  uint32_t mask = 0xffffffff;
  if (spr.x < 0) {
    mask <<= -2 * spr.x;
  }
  if (spr.x > WIDTH - 16) {
    mask >>= 2 * (spr.x - (WIDTH - 16));
  }

  // lines of this strip covered by the sprite, minus MAME's clip band
  int top = 8 * row;
  int y0 = max(spr.y, max(top, SPRITE_MIN_LINE));
  int y1 = min(spr.y + 16, top + 8);
  if (y0 >= y1) {
    return;
  }
  rows.skip(y0 - spr.y);

  unsigned short *ptr = frame_buffer + WIDTH * (y0 - top) + spr.x;
  for (int y = y0; y < y1; y++, ptr += WIDTH) {
    uint32_t pix = rows.next() & mask;
    for (int c = 0; c < 16; c++, pix >>= 2) {
      if (pix & 3) {
        ptr[c] = pal[pix & 3];
      }
    }
  }
}

// MAME order: solid + chars, low sprites, priority tiles, high sprites
void thepit::render_row(short row) {
  if (row < FIRST_ROW || row > LAST_ROW) {
    return;
  }

  Span prio[TILE_SLOTS];
  uint8_t prio_count = draw_tiles(row, prio);

  if (draw_sprites(row, 0, first_high)) {
    for (uint8_t i = 0; i < prio_count; i++) {
      fill_span(prio[i]);
    }
  }
  draw_sprites(row, first_high, active_sprites);
}

const Logo &thepit::logo(void) {
  return thepit_logo;
}

#ifdef LED_PIN
// Task 5 derives the colors from the logo
void thepit::menuLeds(CRGB *leds) {
  fill_solid(leds, NUM_LEDS, LED_BLACK);
}

void thepit::gameLeds(CRGB *leds) {
  fill_solid(leds, NUM_LEDS, LED_BLACK);
}
#endif
