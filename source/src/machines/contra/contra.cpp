#include "../../catalog.h"
#ifdef GAME_CONTRA

#include "contra.h"
#include "contra_rom_main.h"
#include "contra_prom.h"
#include "contra_gfx.h"
#include "contra_sid.h"

// one frame = 264 lines of 384 px at 6 MHz, CPU 3 MHz: 50688 cycles
static constexpr int LINES = 264;
static constexpr int CYCLES_LINE = 192;
static constexpr int VBLANK_LINE = 240;   // K007121 IRQ and sprite latch

static constexpr uint16_t REGS_END = 0x0100;
static constexpr uint16_t BANK_BASE = 0x6000, FIXED_BASE = 0x8000;
static constexpr uint32_t BANK_SIZE = 0x2000;
static constexpr uint32_t FIXED_ROM = 0x18000;   // 8000-ffff = ROM 18000-1ffff
static constexpr uint8_t BANK_MASK = 0x0f;
static constexpr uint16_t BANK_SELECT = 0x7000;

// main loop task poll, left once the IRQ handler clears bit 7 of the flag:
//   8126 LDX <$0E / LDD ,X / ASLA / BCC $8108 / BRA $8126
static constexpr uint16_t IDLE_PC = 0x8126;
static constexpr uint8_t IDLE_PTR = 0x0e;      // direct page pointer to the flag
static constexpr uint8_t IDLE_BUSY = 0x80;

static constexpr uint16_t SND_LATCH = 0x1c;
static constexpr uint16_t SID_FX_ENTRY = 0x04f1;   // Gryzor.sid: effect A = 0-8
static constexpr uint8_t SID_FX_SONG = 3;          // songs 3-11 = effects 0-8

static constexpr uint8_t CTRL_IRQ = 7;           // b1 IRQ enable, 0 acknowledges
static constexpr uint8_t CTRL7_IRQ_ENABLE = 0x02;
static constexpr uint8_t CTRL3_SPRITE_BANK = 0x08;   // sprite list at +0x800

// video, see contra.h
static constexpr int WIDTH = 224;
static constexpr int TOP = 4;                 // panel line of native X 0
static constexpr int NATIVE_W = 280;
static constexpr int HUD_END = 40;            // native X 0..39: text layer
static constexpr int VIS_LAST_Y = 239;
static constexpr int SCROLL_X_BIAS = 40;      // MAME set_scrollx(ctrl0 - 40)
static constexpr int CODES = 16384;           // tile codes per K007121
static constexpr uint8_t LAYER_CHIP[3] = {0, 1, 0};
static constexpr uint16_t LAYER_CRAM[3] = {CONTRA_CRAM0, CONTRA_CRAM1, CONTRA_CRAM2};
static constexpr uint16_t LAYER_VRAM[3] = {CONTRA_VRAM0, CONTRA_VRAM1, CONTRA_VRAM2};
static constexpr int SPRITE_DX = 40;          // MAME set_sprite_offsets(40, 16)
static constexpr int BLOCKS_LIMIT = 15;       // a 32x32 sprite may pass MAX_BLOCKS by 15
static constexpr uint8_t BLOCK_CHIP1 = 0x01, BLOCK_FLIPX = 0x02, BLOCK_FLIPY = 0x04;

// main CPU inputs (konamipt.h KONAMI8_SYSTEM_UNK / KONAMI8_B12_UNK), active low
static constexpr uint8_t SYS_COIN1 = 0x01, SYS_START1 = 0x08;
static constexpr uint8_t P1_LEFT = 0x01, P1_RIGHT = 0x02, P1_UP = 0x04, P1_DOWN = 0x08;
static constexpr uint8_t P1_FIRE = 0x10, P1_JUMP = 0x20;

// jump on the START button: BUTTON_START with a COIN pin (EXTRA is then
// COIN) or the galagino controller, else START itself reports as
// BUTTON_EXTRA (BUTTON_START only follows a virtual coin), as gng
#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int JUMP_KEY = BUTTON_START;
#else
static const unsigned int JUMP_KEY = BUTTON_EXTRA;
#endif

contra::contra()
  : contra_main_rom(contra_main_rom_blob), contra_lut(contra_lut_blob), contra_tiles(contra_tiles_blob),
    contra_tile_index(contra_tile_index_blob), contra_sid(contra_sid_blob) {
  // m6809_reset() reads the reset vector through main_rom
  main_rom = contra_main_rom.data();
  lut = contra_lut.data();
  tiles = contra_tiles.data();
  tile_index = contra_tile_index.data();

  ram = Arena::alloc<uint8_t>(CONTRA_RAM_END);
  vblank = Arena::create<Seqlock<VideoState>>();
  video = Arena::create<VideoState>();
  sprite_latch = (uint8_t(*)[SPRITE_LIST])Arena::alloc<uint8_t>(CHIPS * SPRITE_LIST);
  tune_ok = psidParse(contra_sid.data(), contra_sid.size(), &tune) &&
            player.load(tune, Arena::alloc<uint8_t>(TUNE_RAM_SIZE), TUNE_RAM_SIZE);
  if (!tune_ok) {
    printf("contra: Gryzor.sid is not a supported PSID\n");
  }

  pens = (uint16_t(*)[COLORS][PENS])Arena::alloc<uint16_t>(CHIPS * COLORS * PENS);
  blocks = Arena::alloc<SpriteBlock>(CHIPS * (MAX_BLOCKS + BLOCKS_LIMIT));
}

void contra::reset() {
  machineBase::reset();
  memset(ram, 0, CONTRA_RAM_END);
  memset(ctrl, 0, sizeof(ctrl));
  memset(math_regs, 0, sizeof(math_regs));
  product = quotient = remainder = 0;
  bank = 0;
  irq_line = false;
  memset(sprite_latch, 0, CHIPS * SPRITE_LIST);
  snd_wr = snd_rd = 0;
  sid_on = music_on = false;
  pens_valid = false;

  m6809_reset(&main_cpu);
  main_cpu.rom_direct = main_rom + FIXED_ROM;   // must follow m6809_reset()
  main_cpu.rom_base = FIXED_BASE;
  main_cpu.rom_size = 0x10000 - FIXED_BASE;
}

// ---- Main CPU ----

unsigned char GAME_IRAM contra::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

unsigned char GAME_IRAM contra::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= FIXED_BASE) {
    return main_rom[FIXED_ROM + addr - FIXED_BASE];
  }
  if (addr >= BANK_BASE) {
    return main_rom[bank * BANK_SIZE + addr - BANK_BASE];
  }
  if (addr < REGS_END) {
    return readPort(addr);
  }
  return ram[addr];
}

// buttons_get() only for the input ports: it has side effects (virtual
// coin, reset timer)
uint8_t contra::readPort(uint16_t addr) {
  switch (addr) {
    case 0x10:
      return readSystem(input->buttons_get());
    case 0x11:
      return readPlayer1(input->buttons_get());
    case 0x12:
      return 0xff;   // player 2
    case 0x14:
      return CONTRA_DSW1;
    case 0x15:
      return CONTRA_DSW2 | (input->demoSoundsOff() ? CONTRA_DSW2_DEMO_SOUND_OFF : 0);
    case 0x16:
      return CONTRA_DSW3;
  }
  if (addr >= 0x08 && addr < 0x10) {
    return readMath(addr - 0x08);
  }
  return ram[addr];   // K007121 scroll RAM 20-5f, 80-bf
}

uint8_t contra::readSystem(unsigned int keys) {
  uint8_t v = 0xff;
  if (keys & BUTTON_COIN) {
    v &= ~SYS_COIN1;
  }
  if (keys & BUTTON_START) {
    v &= ~SYS_START1;
  }
  return v;
}

uint8_t contra::readPlayer1(unsigned int keys) {
  uint8_t v = 0xff;
  if (keys & BUTTON_LEFT) {
    v &= ~P1_LEFT;
  }
  if (keys & BUTTON_RIGHT) {
    v &= ~P1_RIGHT;
  }
  if (keys & BUTTON_UP) {
    v &= ~P1_UP;
  }
  if (keys & BUTTON_DOWN) {
    v &= ~P1_DOWN;
  }
  if (keys & (BUTTON_FIRE | BUTTON_A | BUTTON_L1)) {
    v &= ~P1_FIRE;
  }
  if (keys & (JUMP_KEY | BUTTON_B | BUTTON_R1)) {
    v &= ~P1_JUMP;
  }
  return v;
}

void GAME_IRAM contra::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr == BANK_SELECT) {
    bank = val & BANK_MASK;
    return;
  }
  if (addr >= CONTRA_RAM_END) {
    return;
  }
  ram[addr] = val;
  if (addr >= REGS_END) {
    return;
  }

  if (addr < 0x08) {
    writeCtrl(0, addr, val);
  } else if (addr < 0x10) {
    writeMath(addr - 0x08, val);
  } else if (addr >= 0x60 && addr < 0x68) {
    writeCtrl(1, addr - 0x60, val);
  } else if (addr == SND_LATCH) {
    game_started = 1;   // first sound command: attract runs
    const uint8_t next = (snd_wr + 1) & (SND_RING - 1);
    if (next != snd_rd) {   // full: drop
      snd_ring[snd_wr] = val;
      snd_wr = next;
    }
  }
  // 18 coin counter, 1a sound IRQ, 1e watchdog: ignored
}

void contra::writeCtrl(int chip, uint8_t reg, uint8_t val) {
  // k007121.cpp ctrl_w: clearing the enable bit acknowledges the IRQ
  if (chip == 0 && reg == CTRL_IRQ && !(val & CTRL7_IRQ_ENABLE)) {
    irq_line = false;
    main_cpu.irq_pending = 0;
  }
  ctrl[chip][reg] = val;
}

// ---- 007452 math chip (k007452.cpp) ----

void contra::writeMath(uint8_t reg, uint8_t val) {
  if (reg >= MATH_REGS) {
    return;
  }
  math_regs[reg] = val;

  if (reg == 1) {
    product = (uint16_t)(math_regs[0] * math_regs[1]);
    return;
  }
  if (reg != 5) {
    return;
  }
  const uint16_t dividend = (uint16_t)(math_regs[4] << 8 | math_regs[5]);
  const uint16_t divisor = (uint16_t)(math_regs[2] << 8 | math_regs[3]);
  quotient = divisor ? dividend / divisor : 0xffff;
  remainder = divisor ? dividend % divisor : 0;
}

uint8_t contra::readMath(uint8_t reg) {
  switch (reg) {
    case 0: return product & 0xff;
    case 1: return product >> 8;
    case 2: return remainder & 0xff;
    case 3: return remainder >> 8;
    case 4: return quotient & 0xff;
    case 5: return quotient >> 8;
  }
  return 0;
}

// ---- Frame ----

// at IDLE_PC with the flag still busy: the loop would only spin
bool contra::idle(void) {
  if (main_cpu.PC != IDLE_PC) {
    return false;
  }
  const uint16_t ptr = (uint16_t)(main_cpu.DP << 8 | IDLE_PTR);
  const uint16_t flag = (uint16_t)(m6809_read(&main_cpu, ptr) << 8 | m6809_read(&main_cpu, ptr + 1));
  return m6809_read(&main_cpu, flag) & IDLE_BUSY;
}

// 264 lines; the IRQ is a level (k007121.cpp scanline()): asserted at line
// 240 while enabled, re-offered each line until the game clears it
void contra::run_frame(void) {
  int debt = 0;
  for (int line = 0; line < LINES; line++) {
    if (line == VBLANK_LINE) {
      publishVideo();
      irq_line |= (ctrl[0][CTRL_IRQ] & CTRL7_IRQ_ENABLE) != 0;
    }
    if (irq_line) {
      m6809_irq(&main_cpu);
    }

    debt += CYCLES_LINE;
    while (debt > 0) {
      if (idle()) {
        debt = 0;   // nothing but the IRQ ends the poll, no cycle timers to feed
        break;
      }
      const int c = m6809_step(&main_cpu, 4);
      debt -= c > 0 ? c : 1;
    }
  }
}

// Latch what render reads at line 240. The K007121 draws sprites into a
// framebuffer: the list latched here shows one frame later (MAME: the
// sprite list buffered at the vblank before the drawn frame).
void contra::publishVideo(void) {
  vblank->publish([this](VideoState &v) {
    memcpy(v.palette, &ram[CONTRA_PALETTE], sizeof(v.palette));
    for (int l = 0; l < LAYERS; l++) {
      memcpy(v.cram[l], &ram[LAYER_CRAM[l]], MAP_BYTES);
      memcpy(v.vram[l], &ram[LAYER_VRAM[l]], MAP_BYTES);
    }
    memcpy(v.sprites, sprite_latch, sizeof(v.sprites));
    memcpy(v.ctrl, ctrl, sizeof(v.ctrl));
  });
  latchSprites();
}

// k007121.cpp sprites_buffer(): the list bank selected by ctrl3 b3
void contra::latchSprites(void) {
  for (int c = 0; c < CHIPS; c++) {
    const uint16_t base = c ? CONTRA_SPRITES1 : CONTRA_SPRITES0;
    const uint16_t bank = (ctrl[c][3] & CTRL3_SPRITE_BANK) ? SPRITE_LIST : 0;
    memcpy(sprite_latch[c], &ram[base + bank], SPRITE_LIST);
  }
}

// ---- Video ----

void contra::prepare_frame(void) {
  vblank->read(*video);
  // pens depend on palette RAM only (the lookup PROMs are constant)
  if (!pens_valid || memcmp(pens_palette, video->palette, sizeof(pens_palette))) {
    buildPens();
    memcpy(pens_palette, video->palette, sizeof(pens_palette));
    pens_valid = true;
  }
  block_count = 0;
  listSprites(0);
  listSprites(1);
}

// MAME contra palette(): pen = chip << 11 | color << 4 | pix -> lookup PROM
// (chip << 1 | odd palette) -> palette RAM entry; pen 0 of even palettes
// with lookup 0 stays entry 0. xBGR555 -> RGB565, byte swapped.
void contra::buildPens(void) {
  for (int chip = 0; chip < CHIPS; chip++) {
    for (int color = 0; color < COLORS; color++) {
      const int pal = color >> 4;
      const uint8_t *prom = &lut[((chip << 1) | (pal & 1)) << 8 | (color & 15) << 4];
      opaque[chip][color] = 0;
      for (int pix = 0; pix < PENS; pix++) {
        const uint8_t entry = (!(pal & 1) && !prom[pix]) ? 0 : (uint8_t)(pal << 4 | prom[pix]);
        opaque[chip][color] |= entry ? 1u << pix : 0;
        const uint16_t w = video->palette[2 * entry] | video->palette[2 * entry + 1] << 8;
        const uint16_t r = w & 0x1f, g = (w >> 5) & 0x1f, b = (w >> 10) & 0x1f;
        const uint16_t v = (uint16_t)(r << 11 | (g << 1 | g >> 4) << 5 | b);
        pens[chip][color][pix] = (uint16_t)(v << 8 | v >> 8);
      }
    }
  }
}

// MAME get_tile_info / get_tx_tile_info: code bank bits from the attribute,
// selected by ctrl5 (and masked by ctrl4 on the scrolling layers)
uint16_t contra::tileCode(int layer, int index, uint8_t &color) const {
  const uint8_t *c = video->ctrl[LAYER_CHIP[layer]];
  const uint8_t attr = video->cram[layer][index];
  int bank = ((attr >> ((c[5] & 3) + 3)) & 1) | ((attr >> (((c[5] >> 2) & 3) + 2)) & 2) |
             ((attr >> (((c[5] >> 4) & 3) + 1)) & 4) | ((attr >> ((c[5] >> 6) & 3)) & 8);
  if (layer != 2) {
    const int mask = c[4] >> 4;
    bank = (bank & ~mask) | (c[4] & mask);
    bank = (attr >> 7) | bank << 1 | (c[3] & 1) << 5;
  } else {
    bank = (attr >> 7) | bank << 1;
  }
  color = (uint8_t)((c[6] & 0x30) * 2 + 16 + (attr & 7));
  return (uint16_t)(video->vram[layer][index] + bank * 256);
}

// One panel line = native column x: walk native Y 239 down to 16, tile by
// tile; a tile word holds the 8 pixels of one tile column, Y descending.
void contra::drawLayer(int layer, int x, uint16_t *dst, bool opaque) const {
  const uint8_t *c = video->ctrl[LAYER_CHIP[layer]];
  const bool scrolls = layer != 2;
  const int px = (x + (scrolls ? c[0] - SCROLL_X_BIAS : 0)) & 0xff;
  const int py0 = (VIS_LAST_Y + (scrolls ? c[2] : 0)) & 0xff;
  const uint16_t chip_codes = LAYER_CHIP[layer] * CODES;

  for (int col = 0; col < WIDTH;) {
    const int py = (py0 - col) & 0xff;
    const int first = 7 - (py & 7);
    const int n = 8 - first < WIDTH - col ? 8 - first : WIDTH - col;
    uint8_t color;
    const uint16_t code = tileCode(layer, (py >> 3) * 32 + (px >> 3), color);
    uint32_t w = tiles[tile_index[chip_codes + code]][px & 7] >> (4 * first);
    const uint16_t *pen = pens[LAYER_CHIP[layer]][color];
    for (int k = 0; k < n; k++, w >>= 4) {
      const uint8_t pix = w & 15;
      if (pix || opaque) {
        dst[col + k] = pen[pix];
      }
    }
    col += n;
  }
}

// k007121.cpp sprites_draw(): the list ends where its 8x8 blocks reach 264;
// each sprite is 1-16 blocks, code bits from bytes 0, 1 and 4
void contra::listSprites(int chip) {
  static const uint8_t X_OFFSET[4] = {0x0, 0x1, 0x4, 0x5};
  static const uint8_t Y_OFFSET[4] = {0x0, 0x2, 0x8, 0xa};
  const uint8_t *src = video->sprites[chip];
  const uint8_t color_bank = (video->ctrl[chip][6] & 0x30) << 1;

  for (int used = 0, i = 0; used < MAX_BLOCKS; i++, src += SPRITE_BYTES) {
    const uint8_t attr = src[4];
    int w = 1, h = 1, align = 0;
    switch (attr & 0x0e) {
      case 0x02: w = 2; align = 1; break;
      case 0x04: h = 2; align = 2; break;
      case 0x00: w = h = 2; align = 3; break;
      case 0x08: w = h = 4; align = 0xf; break;
    }
    used += w * h;

    int number = (src[0] + ((src[1] & 3) << 8) + ((attr & 0xc0) << 4)) << 2;
    number = (number + ((src[1] >> 2) & 3)) & ~align;
    const int sx = src[3] - ((attr & 1) ? 256 : 0) + SPRITE_DX;
    const int sy = src[2] >= 240 ? src[2] - 256 : src[2];
    const bool flipx = attr & 0x10, flipy = attr & 0x20;
    const uint8_t color = (uint8_t)((src[1] >> 4) + color_bank);
    const uint8_t flags = (uint8_t)((chip ? BLOCK_CHIP1 : 0) | (flipx ? BLOCK_FLIPX : 0) | (flipy ? BLOCK_FLIPY : 0));

    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        const int code = number + X_OFFSET[flipx ? w - 1 - x : x] + Y_OFFSET[flipy ? h - 1 - y : y];
        const int bx = sx + x * 8, by = sy + y * 8;
        if (bx + 8 <= HUD_END || bx >= NATIVE_W || by + 8 <= 16 || by > VIS_LAST_Y) {
          continue;
        }
        SpriteBlock &b = blocks[block_count++];
        b.x = (int16_t)bx;
        b.col = (int16_t)(VIS_LAST_Y - by - 7);
        b.tile = tile_index[chip * CODES + code];
        b.color = color;
        b.flags = flags;
      }
    }
  }
}

// blocks over the strip's lines, clipped to X 40..279 and the 224 columns
void contra::drawSprites(short row) {
  const int x0 = row * 8 - TOP;
  for (int i = 0; i < block_count; i++) {
    const SpriteBlock &b = blocks[i];
    if (b.x + 8 <= x0 || b.x >= x0 + 8) {
      continue;
    }
    const int chip = b.flags & BLOCK_CHIP1;
    const uint16_t *pen = pens[chip][b.color];
    const uint16_t mask = opaque[chip][b.color];

    for (int line = 0; line < 8; line++) {
      const int x = b.x + line;
      if (x < x0 || x >= x0 + 8 || x < HUD_END || x >= NATIVE_W) {
        continue;
      }
      uint32_t w = tiles[b.tile][(b.flags & BLOCK_FLIPX) ? 7 - line : line];
      if (b.flags & BLOCK_FLIPY) {
        w = rev4bpp(w);
      }
      uint16_t *dst = frame_buffer + (x - x0) * WIDTH;
      for (int k = 0; k < 8; k++, w >>= 4) {
        const int col = b.col + k;
        const uint8_t pix = w & 15;
        if ((mask >> pix & 1) && col >= 0 && col < WIDTH) {
          dst[col] = pen[pix];
        }
      }
    }
  }
}

// MAME screen_update: background pen, layer 1, layer 0, sprites #0, #1
// (X 40..279); text layer over X 0..39
void contra::render_row(short row) {
  const uint16_t bg = pens[1][(video->ctrl[1][6] & 0x30) * 2 + 16][0];
  for (int i = 0; i < 8; i++) {
    const int x = row * 8 + i - TOP;
    if (x < 0 || x >= NATIVE_W) {
      continue;
    }
    uint16_t *dst = frame_buffer + i * WIDTH;
    if (x < HUD_END) {
      for (int col = 0; col < WIDTH; col++) {
        dst[col] = bg;
      }
      drawLayer(2, x, dst, false);
      continue;
    }
    drawLayer(1, x, dst, true);   // opaque: covers the background pen
    drawLayer(0, x, dst, false);
  }
  drawSprites(row);
}

// ---- Sound ----

// runs the tune's 6502 code, so on the audio core
void contra::cue(uint8_t cmd) {
  const ContraCue c = contraCue(cmd);
  switch (c.kind) {
    case CUE_STOP:
      sid_on = music_on = false;
      break;
    case CUE_MUSIC:
      player.start(c.arg);
      sid_on = music_on = true;
      break;
    case CUE_FX:
      if (music_on) {
        player.jsr(SID_FX_ENTRY, c.arg);
        break;
      }
      player.start(SID_FX_SONG + c.arg);
      sid_on = true;
      break;
    case CUE_NONE:
      break;
  }
}

void contra::renderAudio(int16_t *out, int n) {
  while (snd_rd != snd_wr) {
    const uint8_t cmd = snd_ring[snd_rd];
    snd_rd = (snd_rd + 1) & (SND_RING - 1);
    if (tune_ok) {
      cue(cmd);
    }
  }

  if (!sid_on) {
    memset(out, 0, n * sizeof(*out));
    return;
  }
  player.render(out, n);
  audioClampAll(out, n, AUDIO_MAX);
}

#ifdef LED_PIN
// logo colors: red, rust, beige
static const CRGB CONTRA_LED_RED(0xa50000), CONTRA_LED_RUST(0x843110), CONTRA_LED_BEIGE(0xb8ac91);

void contra::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      CONTRA_LED_BEIGE, CONTRA_LED_RUST, CONTRA_LED_RED, CONTRA_LED_RED,
      CONTRA_LED_RED, CONTRA_LED_RUST, CONTRA_LED_BEIGE};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// red while FIRE is held
void contra::gameLeds(CRGB *leds) {
  if (input->buttons_get() & BUTTON_FIRE) {
    fill_solid(leds, NUM_LEDS, CONTRA_LED_RED);
    return;
  }
  menuLeds(leds);
}
#endif

#endif // GAME_CONTRA
