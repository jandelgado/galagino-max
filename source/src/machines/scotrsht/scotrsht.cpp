// Scooter Shooter (Konami 1985, GX545), MAME konami/scotrsht.cpp.
//
//   main 6809 (51200 cycles)       sound Z80 (51200 cycles)
//   3100 W latch ---------------> IRQ while pending, 8000 R clears it
//   vblank IRQ (HOLD_LINE)         YM2203 timer B: music tempo (444 Hz)
//
// Both CPUs run interleaved in slices; a latch write reaches the sound CPU
// in the next sound slice.
#include "../../catalog.h"
#ifdef GAME_SCOTRSHT

#include "scotrsht.h"
#include "scotrsht_logo.h"
#include "../../emulation/input.h"
#include "scotrsht_rom.h"
#include "scotrsht_sound.h"
#include "scotrsht_tilemap.h"
#include "scotrsht_spritemap.h"
#include "scotrsht_palette.h"
#include "scotrsht_lut.h"

// Z80 opcode cycle table (static in Tables.h). A host test including
// Z80.c already has it in the same translation unit.
#ifndef SCOTRSHT_Z80_TABLES_INCLUDED
#include "../../cpus/z80/Tables.h"
#endif

static const uint16_t ROM_BASE = 0x4000;
static const uint16_t SND_ROM_END = 0x4000;
static const uint16_t SND_RAM = 0x4000, SND_RAM_END = 0x4400;
static const uint16_t SND_LATCH = 0x8000;
static const uint8_t YM_ADDR_PORT = 0x00;

static const uint8_t CTRL_IRQ = 0x02;  // 2044 b1, b3 flip ignored (upright)
static const uint8_t PAL_BANK_SHIFT = 4, PAL_BANK_MASK = 7;  // 3000 b4-6
static const uint8_t TILE_PAL = 0x80;  // tiles use colors 80-ff
static const uint8_t LUT_SPR_SHIFT = 4;  // scotrsht_lut: high nibble sprite

static const uint8_t OP_EI = 0xfb;
static const int MIN_STEP_CYCLES = 4;
static const int INT_CYCLES = 13;  // IM 1 acknowledge
static const uint8_t YM_TIMER_B = 0x02;

// FM output: 1943 levels (one chip)
static const uint32_t FM_RATE = 24000;
static const int FM_SHIFT = 9;

// Sound idle loop (MAME dasm), waits for the timer B flag:
//   021c EI / 021d IN A,(0) / AND 02h / JR Z,021d
// 69 % of the sound CPU's instructions (MAME trace). While the flag is
// clear and no latch is pending, whole passes are charged up to the
// slice end or the timer B expiry, whichever comes first.
static const uint16_t IDLE_PC = 0x021d;
static const uint8_t IDLE_LOOP[] = {0xfb, 0xdb, 0x00, 0xe6, 0x02, 0x28, 0xfa};
static const int IDLE_PASS_CYCLES = 30;  // IN 11 + AND 7 + JR 12

// order: largest first, as the Arena plan packs them
scotrsht::scotrsht()
  : scotrsht_spritemap(scotrsht_spritemap_blob), scotrsht_rom(scotrsht_rom_blob),
    scotrsht_tilemap(scotrsht_tilemap_blob), scotrsht_sound(scotrsht_sound_blob) {
  sprites = scotrsht_spritemap.data();
  rom_ptr = scotrsht_rom.data();
  tiles = scotrsht_tilemap.data();
  sound_ptr = scotrsht_sound.data();
  idle_ok = !memcmp(sound_ptr + IDLE_PC - 1, IDLE_LOOP, sizeof(IDLE_LOOP));

  // sprite pens with lookup nibble 0 are transparent, whatever the bank
  for (int c = 0; c < 16; c++) {
    spr_transp[c] = 0;
    for (int p = 0; p < 16; p++) {
      if (!(scotrsht_lut[c * 16 + p] >> LUT_SPR_SHIFT)) {
        spr_transp[c] |= 1 << p;
      }
    }
  }
}

void scotrsht::reset() {
  machineBase::reset();
  main_debt = snd_debt = 0;
  irq_enable = irq_line = false;
  charbank = palbank = 0;
  soundlatch = 0;
  latch_pending = false;
  ym.reset();
  fm.reset();

  m6809_reset(&main_cpu);
  // opcode fetches from ROM straight from rom_direct; must follow
  // m6809_reset(), which clears it
  main_cpu.rom_direct = rom_ptr;
  main_cpu.rom_base = ROM_BASE;
  main_cpu.rom_size = 0x10000 - ROM_BASE;
}

// ── 6809 ──

unsigned char GAME_IRAM scotrsht::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

unsigned char GAME_IRAM scotrsht::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= ROM_BASE) {
    return rom_ptr[addr - ROM_BASE];
  }
  if (addr < SC_MEM_END) {
    return memory[addr];
  }

  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xff;
  switch (addr) {
    case 0x3100:
      return SCOTRSHT_DSW2 | (input->demoSoundsOff() ? SCOTRSHT_DSW2_DEMO_SOUND_OFF : 0);
    case 0x3200:
      return SCOTRSHT_DSW3;
    case 0x3300:  // SYSTEM
      if (keys & BUTTON_COIN) { v &= ~0x01; }
      if (keys & BUTTON_START) { v &= ~0x08; }
      return v;
    case 0x3301:  // P1: 8-way stick, one button
      if (keys & BUTTON_LEFT) { v &= ~0x01; }
      if (keys & BUTTON_RIGHT) { v &= ~0x02; }
      if (keys & BUTTON_UP) { v &= ~0x04; }
      if (keys & BUTTON_DOWN) { v &= ~0x08; }
      if (keys & BUTTON_FIRE) { v &= ~0x10; }
      return v;
    case 0x3303:
      return SCOTRSHT_DSW1;
    default:
      return 0xff;  // 3302 P2 (cocktail), unmapped
  }
}

void GAME_IRAM scotrsht::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr < SC_MEM_END) {
    memory[addr] = val;
    return;
  }
  switch (addr) {
    case 0x2043:
      charbank = val & 1;
      return;
    case 0x2044:  // written on every idle loop pass
      irq_enable = val & CTRL_IRQ;
      if (irq_enable) { game_started = 1; }
      return;
    case 0x3000:  // b0-1 coin counters
      palbank = (val >> PAL_BANK_SHIFT) & PAL_BANK_MASK;
      return;
    case 0x3100:
      soundlatch = val;
      latch_pending = true;
      return;
    default:
      return;  // ROM, 2040-2042, 3200, 3300 watchdog
  }
}

// ── sound Z80 ──

unsigned char GAME_IRAM scotrsht::opZ80(unsigned short Addr) {
  return Addr < SND_ROM_END ? sound_ptr[Addr] : 0xff;
}

unsigned char GAME_IRAM scotrsht::rdZ80(unsigned short Addr) {
  if (Addr < SND_ROM_END) {
    return sound_ptr[Addr];
  }
  if (Addr >= SND_RAM && Addr < SND_RAM_END) {
    return memory[SC_MEM_SND + Addr - SND_RAM];
  }
  if (Addr == SND_LATCH) {
    latch_pending = false;
    return soundlatch;
  }
  return 0xff;
}

void GAME_IRAM scotrsht::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= SND_RAM && Addr < SND_RAM_END) {
    memory[SC_MEM_SND + Addr - SND_RAM] = Value;
  }
}

// YM2203 at ports 00 (status / address) and 01 (data)
unsigned char scotrsht::inZ80(unsigned short Port) {
  return (Port & 0xff) == YM_ADDR_PORT ? ym.readStatus() : 0xff;
}

void scotrsht::outZ80(unsigned short Port, unsigned char Value) {
  if ((Port & 0xff) == YM_ADDR_PORT) {
    ym.writeAddress(Value);
    return;
  }
  ym.writeData(Value);
}

// ICount starts at 0 every step: block opcodes (LDIR) loop while ICount > 0
int scotrsht::step_sound() {
  const uint8_t op = opZ80(cpu[0].PC.W);
  cpu[0].ICount = -Cycles[op];
  StepZ80(&cpu[0]);

  // EI parks ICount at 1 for RunZ80; undo that
  if (op == OP_EI) {
    cpu[0].ICount = -Cycles[op];
  }
  const int used = -cpu[0].ICount;
  return used > MIN_STEP_CYCLES ? used : MIN_STEP_CYCLES;
}

// A pending latch ends the skip only while its IRQ can be taken: the IRQ
// handler returns with interrupts off, the EI at 021c follows the next tick.
bool scotrsht::sound_idle() const {
  return cpu[0].PC.W == IDLE_PC && idle_ok && !(latch_pending && (cpu[0].IFF & IFF_1)) &&
         !(ym.readStatus() & YM_TIMER_B);
}

// Timer B ticks with the Z80 cycles (same 3.072 MHz clock).
void scotrsht::run_sound(int cycles) {
  snd_debt += cycles;
  while (snd_debt > 0) {
    if (latch_pending && (cpu[0].IFF & IFF_1)) {
      IntZ80(&cpu[0], INT_RST38);
      ym.tick(INT_CYCLES);
      snd_debt -= INT_CYCLES;
    }

    if (sound_idle()) {
      uint32_t budget = snd_debt;
      const uint32_t to_b = ym.cyclesToTimerB();
      if (to_b < budget) {
        budget = to_b;
      }
      const uint32_t spun = (budget + IDLE_PASS_CYCLES - 1) / IDLE_PASS_CYCLES * IDLE_PASS_CYCLES;
      ym.tick(spun);
      snd_debt -= spun;
      continue;
    }

    const int used = step_sound();
    ym.tick(used);
    snd_debt -= used;
  }
}

// ── main 6809 ──

// Main CPU idle loop (MAME dasm), waits for a task in the queue:
//   406f LDA $1D80 / STA $2044 / LDX $195A / LDD ,X / ASLA / BCS $406F
// Only the vblank IRQ handler fills the queue. While PC is on the LDA,
// the slot is still empty and no IRQ can be taken, the rest of the slice
// is charged in whole passes. A pass rewrites 2044 from 1d80: applied once.
static const uint16_t MAIN_IDLE_PC = 0x406f, MAIN_IDLE_END = 0x407d;  // LDA .. after BCS
static const uint16_t IDLE_CTRL = 0x1d80, IDLE_QUEUE_PTR = 0x195a;
static const uint8_t IDLE_SLOT_EMPTY = 0x80;
static const int MAIN_PASS_CYCLES = 26;  // LDA 5 + STA 5 + LDX 6 + LDD 5 + ASLA 2 + BCS 3

bool scotrsht::in_main_loop() const {
  return main_cpu.PC > MAIN_IDLE_PC && main_cpu.PC < MAIN_IDLE_END;
}

bool scotrsht::main_idle() {
  if (main_cpu.PC != MAIN_IDLE_PC) {
    return false;
  }
  if (irq_line && !(main_cpu.CC & M6809_CC_I)) {
    return false;
  }
  const uint16_t slot = (memory[IDLE_QUEUE_PTR] << 8) | memory[IDLE_QUEUE_PTR + 1];
  return m6809_read(&main_cpu, slot) & IDLE_SLOT_EMPTY;
}

void scotrsht::run_main(int cycles) {
  main_debt += cycles;
  while (main_debt > 0) {
    if (main_idle()) {
      m6809_write(&main_cpu, 0x2044, memory[IDLE_CTRL]);
      main_debt -= (main_debt + MAIN_PASS_CYCLES - 1) / MAIN_PASS_CYCLES * MAIN_PASS_CYCLES;
      break;
    }
    if (irq_line) {
      m6809_irq(&main_cpu);
      irq_line = main_cpu.irq_pending;  // held until taken
    }
    // single steps inside the loop: a 4 step can't drift onto its LDA
    const int c = m6809_step(&main_cpu, in_main_loop() ? 1 : 4);
    main_debt -= c > 0 ? c : 1;
  }
}

// MAME: vblank IRQ at the end of the visible frame.
void scotrsht::run_frame(void) {
  for (int i = 0; i < SLICES; i++) {
    run_main(SLICE_CYCLES);
    run_sound(SLICE_CYCLES);
  }
  publish_video();
  if (irq_enable) {
    irq_line = true;
  }
}

// Latch what render reads, where MAME raises vblank.
void scotrsht::publish_video() {
  vblank.publish([this](VideoState &s) {
    for (int ty = 0; ty < TILE_ROWS; ty++) {
      memcpy(s.cram + ty * VIS_COLS, memory + SC_MEM_CRAM + ty * 64, VIS_COLS);
      memcpy(s.vram + ty * VIS_COLS, memory + SC_MEM_VRAM + ty * 64, VIS_COLS);
    }
    memcpy(s.scroll, memory + SC_MEM_SCROLL, sizeof(s.scroll));
    memcpy(s.spr, memory + SC_MEM_SPR, sizeof(s.spr));
    s.charbank = charbank;
    s.palbank = palbank;
  });
}

// ── video core ──

// MAME palette(): tile pen = rgb[80 | bank << 4 | lut7f], sprite pen =
// rgb[bank << 4 | lut8f]
void scotrsht::build_cmaps(uint8_t bank) {
  for (int c = 0; c < 16; c++) {
    for (int p = 0; p < 16; p++) {
      const uint8_t l = scotrsht_lut[c * 16 + p];
      tile_cmap[c][p] = scotrsht_palette[TILE_PAL | bank << 4 | (l & 0x0f)];
      spr_cmap[c][p] = scotrsht_palette[bank << 4 | l >> LUT_SPR_SHIFT];
    }
  }
  cmap_bank = bank;
}

// Sprites [code, attr, sx, sy], attr: b7 sx bit 8 (subtract), b6 code bit 8,
// b5 flipy, b4 flipx, b0-3 color. Native (sx, sy) -> panel x 224 - sy,
// panel y sx + 16. MAME order: entry 0 first, later ones on top.
void scotrsht::prepare_frame(void) {
  vblank.read(video);
  if (video.palbank != cmap_bank) {
    build_cmaps(video.palbank);
  }

  active_sprites = 0;
  for (int offs = 0; offs < SPRITE_BYTES; offs += 4) {
    const uint8_t *s = video.spr + offs;
    const uint8_t attr = s[1];
    const int sx = s[2] - ((attr & 0x80) << 1);
    sprite_S spr;
    spr.x = 224 - s[3];
    spr.y = sx + 16;
    if (spr.x <= -16 || spr.x >= 224 || spr.y <= 0 || spr.y >= 272) {
      continue;
    }
    spr.code = s[0];
    spr.color_block = (attr >> 6) & 1;  // code bit 8
    spr.color = attr & 0x0f;
    spr.flags = (attr >> 4) & 3;        // b0 native flipx, b1 native flipy
    sprite[active_sprites++] = spr;
  }
}

// Landscape sprite data, rotated while drawn: ROM row r -> panel x
// spr_x + 15 - r, ROM column c -> panel y spr_y + c (circusc).
void scotrsht::blit_spr(short row, unsigned char s) {
  const sprite_S &sp = sprite[s];
  const uint8_t (*spr)[8] = sprites[sp.code | (sp.color_block << 8)];
  const uint16_t *pal = spr_cmap[sp.color];
  const uint16_t transp = spr_transp[sp.color];
  const uint8_t flipx = sp.flags & 1;         // native columns (panel y)
  const uint8_t flipy = (sp.flags >> 1) & 1;  // native rows (panel x)
  const int row_start = 8 * row;

  const int y_begin = sp.y > row_start ? sp.y : row_start;
  const int y_end = sp.y + 15 < row_start + 7 ? sp.y + 15 : row_start + 7;
  if (y_begin > y_end) {
    return;
  }

  for (int r = 0; r < 16; r++) {
    const int screen_x = sp.x + 15 - r;
    if (screen_x < 0 || screen_x >= 224) {
      continue;
    }
    const uint8_t *row_data = spr[flipy ? 15 - r : r];
    for (int screen_y = y_begin; screen_y <= y_end; screen_y++) {
      const int c = screen_y - sp.y;
      const int sc = flipx ? 15 - c : c;
      const uint8_t px = (row_data[sc >> 1] >> ((sc & 1) << 2)) & 0x0f;
      if (!((transp >> px) & 1)) {
        frame_buffer[(screen_y - row_start) * 224 + screen_x] = pal[px];
      }
    }
  }
}

// Pre-rotated 8x8 tile at panel x (may be clipped), opaque.
void scotrsht::blit_tile(unsigned short addr, short x) {
  const uint8_t attr = video.cram[addr];
  const unsigned short code = video.vram[addr] + (video.charbank << 9) + ((attr & 0x40) << 2);
  const uint8_t (*tile)[4] = tiles[code];
  const uint16_t *pal = tile_cmap[attr & 0x0f];

  // native flipx (b4) mirrors panel rows, native flipy (b5) panel columns
  const uint8_t flip_y = (attr >> 4) & 1;
  const uint8_t flip_x = (attr >> 5) & 1;

  const uint8_t c0 = x < 0 ? -x : 0;
  const uint8_t c1 = x > 224 - 8 ? 224 - x : 8;

  unsigned short *ptr = frame_buffer + x;
  for (uint8_t r = 0; r < 8; r++, ptr += 224) {
    const uint8_t *trow = tile[flip_y ? 7 - r : r];

    // unclipped: 2 pixels per byte, low nibble first
    if (c0 == 0 && c1 == 8) {
      for (uint8_t i = 0; i < 4; i++) {
        const uint8_t b = trow[flip_x ? 3 - i : i];
        ptr[2 * i] = pal[flip_x ? b >> 4 : b & 0x0f];
        ptr[2 * i + 1] = pal[flip_x ? b & 0x0f : b >> 4];
      }
      continue;
    }

    for (uint8_t c = c0; c < c1; c++) {
      const uint8_t sc = flip_x ? 7 - c : c;
      ptr[c] = pal[(trow[sc >> 1] >> ((sc & 1) << 2)) & 0x0f];
    }
  }
}

// Strip r shows native tile column tx = r - 2, scrolled by scroll[tx]:
// tile row ty at panel x (232 - 8 * ty + scroll) & 0xff, wraps at 256.
void scotrsht::render_row(short row) {
  if (row < 2 || row >= 34) {
    return;
  }

  const uint8_t tx = row - 2;
  const uint8_t scr = video.scroll[tx];
  for (uint8_t ty = 0; ty < TILE_ROWS; ty++) {
    short x = (232 - 8 * ty + scr) & 0xff;
    if (x >= 224) {
      x -= 256;
    }
    if (x > -8) {
      blit_tile(ty * VIS_COLS + tx, x);
    }
  }

  for (unsigned char s = 0; s < active_sprites; s++) {
    if (sprite[s].y < 8 * (row + 1) && sprite[s].y + 16 > 8 * row) {
      blit_spr(row, s);
    }
  }
}

// ── audio, video core ──

void scotrsht::renderAudio(int16_t *out, int n) {
  static const int CHUNK = 64;  // Audio renders 64 samples per call
  for (int done = 0; done < n; done += CHUNK) {
    const int len = n - done < CHUNK ? n - done : CHUNK;
    int32_t acc[CHUNK] = {};
    fm.render(acc, len, FM_RATE);
    for (int i = 0; i < len; i++) {
      out[done + i] = (int16_t)(acc[i] >> FM_SHIFT);
    }
  }

  int16_t ssg[AUDIO_CHUNK];
  ym.renderSsg(ssg, n);
  audioAdd(out, ssg, n);
  audioClampAll(out, n, AUDIO_MAX);
}

const Logo &scotrsht::logo(void) {
  return scotrsht_logo;
}

#ifdef LED_PIN
// from logos/scotrsht.png: cyan, pink
static const CRGB SCOTRSHT_LED_CYAN = CRGB(0x00c2f8);
static const CRGB SCOTRSHT_LED_PINK = CRGB(0xe03f92);

void scotrsht::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      SCOTRSHT_LED_PINK, SCOTRSHT_LED_CYAN, SCOTRSHT_LED_CYAN, SCOTRSHT_LED_CYAN,
      SCOTRSHT_LED_CYAN, SCOTRSHT_LED_CYAN, SCOTRSHT_LED_PINK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// blink pink while FIRE is held, else the menu colors
void scotrsht::gameLeds(CRGB *leds) {
  fireBlinkLeds(leds, input, led_frame, SCOTRSHT_LED_PINK, menuLeds);
}
#endif

#endif // GAME_SCOTRSHT
