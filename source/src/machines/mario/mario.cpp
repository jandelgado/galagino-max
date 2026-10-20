#include "mario.h"
#include "mario_rom_main.h"
#include "mario_tiles.h"
#include "mario_sprites.h"
#include "mario_palette.h"
#include "mario_logo.h"
#include "../../emulation/spriteflip.h"

// Z80 opcode cycle table (static in Tables.h). A host test including
// Z80.c already has it in the same translation unit.
#ifndef MARIO_Z80_TABLES_INCLUDED
#include "../../cpus/z80/Tables.h"
#endif

mario::mario()
  : mario_rom_main(mario_rom_main_blob),
    mario_tiles(mario_tiles_blob),
    mario_sprites(mario_sprites_blob),
    mario_palette(mario_palette_blob) {
  rom = mario_rom_main.data();
  tiles = mario_tiles.data();
  sprite_gfx = mario_sprites.data();
  palette = mario_palette.data();
}

const Logo &mario::logo(void) {
  return mario_logo;
}

void mario::reset() {
  machineBase::reset();
  latch = 0;
  scroll = 0;
  tune = 0;
  pan = 0;
}

unsigned char mario::opZ80(unsigned short Addr) {
  if (Addr >= ROM_HIGH) {
    return rom[Addr - ROM_HIGH + ROM_HIGH_OFS];
  }
  return rom[Addr];
}

uint8_t mario::in0() {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0;

  if (keys & BUTTON_RIGHT) {
    v |= IN0_RIGHT;
  }
  if (keys & BUTTON_LEFT) {
    v |= IN0_LEFT;
  }
  if (keys & (BUTTON_FIRE | BUTTON_UP | BUTTON_A)) {
    v |= IN0_JUMP;
  }
  if (keys & BUTTON_START) {
    v |= IN0_START1;
  }
  return v;
}

unsigned char mario::rdZ80(unsigned short Addr) {
  if (Addr < RAM_BASE || Addr >= ROM_HIGH) {
    return opZ80(Addr);
  }
  if (Addr < RAM_END) {
    return memory[Addr - RAM_BASE];
  }
  if (Addr == IN0_ADDR) {
    return in0();
  }
  if (Addr == IN1_ADDR) {
    return (input->buttons_get() & BUTTON_COIN) ? IN1_COIN1 : 0;
  }
  if (Addr == DSW_ADDR) {
    return MARIO_DIP;
  }
  return 0x00;
}

void mario::writeLatch(uint8_t bit, uint8_t value) {
  const uint8_t mask = 1 << bit;
  const uint8_t old = latch;
  latch = (value & 1) ? (latch | mask) : (latch & ~mask);

  // DMA rdy: the game programs the Z80 DMA every frame for 0x180 bytes
  // 6900 -> 7000 (measured in MAME), so the transfer is a fixed copy
  if (bit == LATCH_DMA && (latch & ~old & mask)) {
    memcpy(memory + SPRITE_RAM, memory + DMA_SRC, SPRITE_BYTES);
  }
  if (bit == LATCH_NMI && (latch & mask)) {
    game_started = 1;
  }
}

void mario::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= RAM_BASE && Addr < RAM_END) {
    memory[Addr - RAM_BASE] = Value;
    return;
  }
  if (Addr == SCROLL_ADDR) {
    scroll = Value;
    return;
  }
  if (Addr == TUNE_ADDR) {
    tune = Value;
    return;
  }
  if ((Addr & ~7) == LATCH_ADDR) {
    writeLatch(Addr & 7, Value);
  }
}

// Runs the main CPU for `cycles`. StepZ80 only counts prefixed opcodes,
// so the main opcode's cycles are charged here (Tables.h Cycles[]).
void mario::runZ80(int32_t cycles) {
  Z80 &z = cpu[0];
  z.ICount += cycles;
  while (z.ICount > 0) {
    z.ICount -= Cycles[opZ80(z.PC.W)];
    StepZ80(&z);
  }
}

void mario::run_frame(void) {
  current_cpu = 0;
  for (uint8_t i = 0; i < SLICES; i++) {
    runZ80(Z80_CYCLES_FRAME / SLICES);
  }

  vblank.publish([this](VideoState &v) {
    memcpy(v.tiles, memory + TILE_RAM, TILE_BYTES);
    memcpy(v.sprites, memory + SPRITE_RAM, SPRITE_BYTES);
    v.scroll = scroll;
    v.latch = latch;
    v.player1_x = memory[PLAYER1_X];
  });

  if (latch & (1 << LATCH_NMI)) {
    IntZ80(&cpu[0], INT_NMI);
  }
}

uint8_t mario::nativeX(uint16_t col, uint8_t pan) {
#if defined(MARIO_CROP)
  (void)pan;
  return mario_video::crop_x(col);
#elif defined(MARIO_SCALE)
  (void)pan;
  return mario_video::scale_x(col);
#else
  return (uint8_t)(col + pan);
#endif
}

void mario::prepare_frame(void) {
  vblank.read(video);
  const bool flip = video.latch & (1 << LATCH_FLIP);
  const uint8_t pal = (video.latch & (1 << LATCH_PAL_BANK)) ? 16 : 0;

  // MAME draw_sprites: screen position from the 4 byte entry, the flip
  // latch mirrors position and image
  sprite_count = 0;
  for (uint8_t i = 0; i < SPRITES; i++) {
    const uint8_t *s = video.sprites + 4 * i;
    if (!s[0]) {
      continue;
    }

    Sprite &o = sprite_list[sprite_count++];
    int16_t y = 240 - ((s[0] + (flip ? 0xf8 : 0xfa)) & 0xff);
    int16_t x = s[3];
    uint8_t f = ((s[1] & 0x80) ? FLIP_PIXELS : 0) | ((s[1] & 0x40) ? FLIP_ROWS : 0);
    if (flip) {
      y = (y ^ 0xff) - 14;
      x = (x ^ 0xff) - 7;
      f ^= FLIP_PIXELS | FLIP_ROWS;
    } else {
      y += 1;
      x -= 8;
    }
    o.x = x;
    o.y = y;
    o.code = s[2];
    o.pen = ((s[1] & 0x0f) + pal) * 8;
    o.flip = f;
  }

#ifdef MARIO_SCROLL
  // Mario's sprite x on screen (flipped board: x ^ 0xff - 7)
  const uint8_t px = flip ? (uint8_t)((video.player1_x ^ 0xff) - 7) : (uint8_t)(video.player1_x - 8);
  pan = mario_video::pan_step(pan, mario_video::pan_target(px));
#endif
}

// one native screen line of the tilemap into pens[0..255]
void mario::drawTiles(uint8_t line, uint8_t *pens) {
  const bool flip = video.latch & (1 << LATCH_FLIP);
  const uint16_t bank = (video.latch & (1 << LATCH_GFX_BANK)) ? 256 : 0;
  const uint8_t pal = (video.latch & (1 << LATCH_PAL_BANK)) ? 16 : 0;

  // MAME: scrolly = data + 17; a flipped tilemap mirrors the scrolled line
  uint8_t ty = (uint8_t)(line + video.scroll + 17);
  if (flip) {
    ty = 255 - ty;
  }
  const uint8_t *row = video.tiles + 32 * (ty >> 3);

  for (uint8_t c = 0; c < 32; c++) {
    const uint8_t v = row[c];
    uint16_t bits = tiles[v + bank][ty & 7];
    const uint8_t pen = (8 + (v >> 5) + pal) * 8;

    // logical x 8c..8c+7, mirrored on a flipped board
    if (flip) {
      uint8_t *out = pens + 255 - 8 * c;
      for (uint8_t k = 0; k < 8; k++, bits >>= 2) {
        *out-- = pen | (bits & 3);
      }
    } else {
      uint8_t *out = pens + 8 * c;
      for (uint8_t k = 0; k < 8; k++, bits >>= 2) {
        *out++ = pen | (bits & 3);
      }
    }
  }
}

// sprites crossing native line `line`, later entries on top
void mario::drawSprites(uint8_t line, uint8_t *pens) {
  for (uint8_t i = 0; i < sprite_count; i++) {
    const Sprite &o = sprite_list[i];
    const int16_t r = line - o.y;
    if (r < 0 || r >= 16) {
      continue;
    }

    SpriteRows4bpp rows(sprite_gfx[o.code], o.flip);
    rows.skip(r);
    uint32_t px[2];
    rows.next(px);

    for (uint8_t k = 0; k < 16; k++) {
      const uint8_t p = (px[k >> 3] >> (4 * (k & 7))) & 7;
      const int16_t x = o.x + k;
      if (p && x >= 0 && x < 256) {
        pens[x] = o.pen | p;
      }
    }
  }
}

void mario::render_row(short row) {
  if (row < FIRST_ROW || row >= LAST_ROW) {
    return;
  }

  const uint8_t line0 = VIS_TOP + 8 * (row - FIRST_ROW);
  for (uint8_t l = 0; l < 8; l++) {
    drawTiles(line0 + l, pens[l]);
    drawSprites(line0 + l, pens[l]);
  }

  for (uint8_t l = 0; l < 8; l++) {
    unsigned short *out = frame_buffer + l * mario_video::OUT_WIDTH;
    for (uint16_t c = 0; c < mario_video::OUT_WIDTH; c++) {
      out[c] = palette[pens[l][nativeX(c, pan)]];
    }
  }
}

#ifdef LED_PIN
void mario::menuLeds(CRGB *leds) {
  defaultMenuLeds(leds);
}

void mario::gameLeds(CRGB *leds) {
  defaultMenuLeds(leds);
}
#endif
