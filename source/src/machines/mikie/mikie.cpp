#include "../../catalog.h"
#ifdef GAME_MIKIE

#include "mikie.h"
#include "mikie_rom_main.h"
#include "mikie_rom_audio.h"
#include "mikie_tiles.h"
#include "mikie_sprites.h"
#include "mikie_palette.h"

static constexpr uint16_t MAIN_ROM_BASE = 0x6000;
static constexpr uint8_t TILE_PRIORITY = 0x10;   // colorram: tile drawn over sprites
static constexpr int VIS_TOP = 16;               // first visible native Y
static constexpr int VIS_END = 240;              // one past last visible native Y

mikie::mikie()
  : mikie_rom_main(mikie_rom_main_blob), mikie_rom_audio(mikie_rom_audio_blob),
    mikie_tiles(mikie_tiles_blob), mikie_sprites(mikie_sprites_blob),
    mikie_rgb(mikie_rgb_blob), mikie_lut(mikie_lut_blob) {
  // m6809_reset() reads the reset vector through main_rom
  main_rom = mikie_rom_main.data();
  audio_rom = mikie_rom_audio.data();
  tiles = mikie_tiles.data();
  sprite_gfx = mikie_sprites.data();
  rgb = mikie_rgb.data();
  lut = mikie_lut.data();
}

void mikie::reset() {
  machineBase::reset();               // clears memory[], mutes SN chips, resets Z80
  irq_mask = sound_on = 0;
  palette_bank = 0;
  soundlatch = snd_irq_pend = 0;
  snd_cycles = 0;
  cmap_bank = -1;

  m6809_reset(&main_cpu);
  main_cpu.rom_direct = main_rom;     // must follow m6809_reset()
  main_cpu.rom_base = MAIN_ROM_BASE;
  main_cpu.rom_size = 0x10000 - MAIN_ROM_BASE;
}

void mikie::start() {
  reset();
  game_started = 1;
}

// ---- Main 6809 ----

unsigned char GAME_IRAM mikie::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

unsigned char GAME_IRAM mikie::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= MAIN_ROM_BASE) {
    return main_rom[addr - MAIN_ROM_BASE];
  }
  if (addr < 0x0100) {
    return memory[MIKIE_ZP + addr];
  }
  if (addr >= 0x2800 && addr < 0x3800) {
    return memory[MIKIE_RAM + (addr - 0x2800)];
  }
  if (addr >= 0x3800 && addr < 0x4000) {
    return memory[MIKIE_CRAM + (addr - 0x3800)];   // colorram, then videoram
  }
  if (addr >= 0x4000) {
    return 0x00;                                    // empty ROM socket 0x4000-0x5FFF
  }
  return read_io(addr);
}

uint8_t mikie::read_io(uint16_t addr) {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xFF;
  switch (addr) {
    case 0x2400:                                         // SYSTEM
      if (keys & BUTTON_COIN)  { v &= ~0x01; }
      if (keys & BUTTON_START) { v &= ~0x08; }
      return v;
    case 0x2401:                                         // P1, 4-way
      if (keys & BUTTON_LEFT)  { v &= ~0x01; }
      if (keys & BUTTON_RIGHT) { v &= ~0x02; }
      if (keys & BUTTON_UP)    { v &= ~0x04; }
      if (keys & BUTTON_DOWN)  { v &= ~0x08; }
      if (keys & (BUTTON_FIRE | BUTTON_A | BUTTON_L1))  { v &= ~0x10; }   // button 1
      if (keys & (BUTTON_START | BUTTON_B | BUTTON_R1)) { v &= ~0x20; }   // button 2
      return v;
    case 0x2403: return MIKIE_DSW3;
    case 0x2500: return MIKIE_DSW1;
    case 0x2501: {
      uint8_t d = MIKIE_DSW2;
      if (input->demoSoundsOff()) { d |= MIKIE_DSW2_DEMO_SOUND_OFF; }
      return d;
    }
  }
  return 0xFF;                                           // P2 0x2402, unmapped
}

// LS259 at 0x2000-0x2007: Q2 rising edge = sound IRQ, Q7 = vblank IRQ
// enable; coin counters (Q0/Q1) and flip (Q6, upright) ignored.
void mikie::write_latch(uint8_t bit, uint8_t val) {
  const uint8_t q = val & 1;
  if (bit == 2) {
    if (q && !sound_on) { snd_irq_pend = 1; }
    sound_on = q;
    return;
  }
  if (bit == 7) {
    irq_mask = q;
    if (!irq_mask) { main_cpu.irq_pending = 0; }
  }
}

void GAME_IRAM mikie::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr < 0x0100) { memory[MIKIE_ZP + addr] = val; return; }
  if (addr >= 0x2800 && addr < 0x3800) { memory[MIKIE_RAM + (addr - 0x2800)] = val; return; }
  if (addr >= 0x3800 && addr < 0x4000) { memory[MIKIE_CRAM + (addr - 0x3800)] = val; return; }
  if ((addr & 0xFFF8) == 0x2000) { write_latch(addr & 7, val); return; }
  if (addr == 0x2200) { palette_bank = val & 0x07; return; }
  if (addr == 0x2400) { soundlatch = val; return; }
  // 0x2100 watchdog, 0x2300 unknown: ignored
}

// ---- Sound Z80 ----

unsigned char mikie::opZ80(unsigned short Addr) { return rdZ80(Addr); }

unsigned char mikie::rdZ80(unsigned short Addr) {
  if (Addr < 0x2000) { return audio_rom[Addr]; }
  if (Addr < 0x4000) { return 0x00; }                    // unpopulated ROM socket
  if ((Addr & 0xFC00) == 0x4000) { return memory[MIKIE_SND_RAM + (Addr & 0x3FF)]; }
  if (Addr == 0x8003) { return soundlatch; }
  if (Addr == 0x8005) { return (uint8_t)(snd_cycles >> 9); }   // MAME: total_cycles / 512
  return 0xFF;
}

void mikie::wrZ80(unsigned short Addr, unsigned char Value) {
  if ((Addr & 0xFC00) == 0x4000) { memory[MIKIE_SND_RAM + (Addr & 0x3FF)] = Value; return; }
  if (Addr == 0x8002) { sn[0].write(Value); return; }
  if (Addr == 0x8004) { sn[1].write(Value); return; }
  // 0x8000/0x8001/0x8079/0xA003: unknown writes, ignored
}

// ---- Frame ----

// One frame = 256 lines from vblank (line 240): main 6809 ~100 cycles per
// line, Z80 20 instructions. The vblank IRQ (MAME ASSERT_LINE) stays
// pending until the game clears Q7.
// Main CPU idle loop (MAME dasm):
//   61bc LDX <$14 / LDD ,X / ASLA / BCC $61a6 / BSR $61c7 / BRA $61bc
//   61c7 TST <$20 / BEQ $61fb;  61fb RTS
// waits for the task queue slot [<$14] to lose bit 7 while <$20 is 0. Only
// the vblank IRQ handler changes them, and the IRQ is raised once, before
// the lines: while PC is on the LDX and both still hold, the rest of the
// line is spinning, charged in whole passes.
static const uint16_t IDLE_PC = 0x61BC, IDLE_END = 0x61CB;  // LDX .. after BEQ
static const uint16_t IDLE_RTS = 0x61FB;
static const uint8_t IDLE_PTR_DP = 0x14, IDLE_BUSY_DP = 0x20;
static const uint8_t IDLE_SLOT_EMPTY = 0x80;
static const int IDLE_PASS_INSTR = 9;
static const int IDLE_PASS_CYCLES = 39;   // one pass in the core's cycles

bool mikie::in_idle_loop() const {
  return (main_cpu.PC > IDLE_PC && main_cpu.PC < IDLE_END) || main_cpu.PC == IDLE_RTS;
}

bool mikie::main_idle() {
  if (main_cpu.PC != IDLE_PC) {
    return false;
  }
  const uint16_t dp = main_cpu.DP << 8;
  if (m6809_read(&main_cpu, dp | IDLE_BUSY_DP)) {
    return false;
  }
  const uint16_t ptr = dp | IDLE_PTR_DP;
  const uint16_t slot = (m6809_read(&main_cpu, ptr) << 8) | m6809_read(&main_cpu, ptr + 1);
  return m6809_read(&main_cpu, slot) & IDLE_SLOT_EMPTY;
}

void mikie::run_frame(void) {
  if (irq_mask) { m6809_irq(&main_cpu); }

  int main_debt = 0;
  for (int n = 0; n < MIKIE_LINES; n++) {
    main_debt += MIKIE_CYCLES_LINE;
    while (main_debt > 0) {
      if (main_idle()) {
        main_debt -= (main_debt + IDLE_PASS_CYCLES - 1) / IDLE_PASS_CYCLES * IDLE_PASS_CYCLES;
        break;
      }
      // single steps inside the loop: a 4 step can't drift onto its LDX
      const int c = m6809_step(&main_cpu, in_idle_loop() ? 1 : 4);
      main_debt -= (c > 0) ? c : 1;
    }

    for (int i = 0; i < MIKIE_Z80_STEPS_LINE; i++) { StepZ80(&cpu[0]); }
    snd_cycles = (snd_cycles + MIKIE_Z80_CYCLES_LINE) & 0x1FFFF;
    if (snd_irq_pend && (cpu[0].IFF & IFF_1)) {
      IntZ80(&cpu[0], INT_RST38);
      snd_irq_pend = 0;
    }
  }

  publish_video();
}

// Latch what render reads, where MAME raises vblank.
void mikie::publish_video() {
  vblank.publish([this](VideoState &v) {
    memcpy(v.vram, &memory[MIKIE_VRAM], sizeof(v.vram));
    memcpy(v.cram, &memory[MIKIE_CRAM], sizeof(v.cram));
    memcpy(v.sprites, &memory[MIKIE_RAM], sizeof(v.sprites));
    v.palette_bank = palette_bank;
  });
}

// ---- Video ----

// MAME palette(): char pen = rgb[bank<<5 | 0x10 | lut[c<<4 | p]],
// sprite pen = rgb[bank<<5 | lut[0x100 | c<<4 | p]].
void mikie::build_cmaps(uint8_t bank) {
  const uint8_t base = bank << 5;
  for (int c = 0; c < 16; c++) {
    for (int p = 0; p < 16; p++) {
      cmap_tiles[c][p] = rgb[base | 0x10 | lut[(c << 4) | p]];
      cmap_sprites[c][p] = rgb[base | lut[0x100 | (c << 4) | p]];
    }
  }
  cmap_bank = bank;
}

void mikie::prepare_frame(void) {
  vblank.read(video);
  if (video.palette_bank != cmap_bank) { build_cmaps(video.palette_bank); }
}

// Strip row 2..33 = display lines 8*(row-2)..+7 = native X 255-line, all
// inside one tile column. Column c = native Y c + 16.
void GAME_IRAM mikie::render_row(short row) {
  if (row < 2 || row >= 34 || !game_started) { return; }   // frame_buffer arrives cleared

  const uint8_t tx = (255 - 8 * (row - 2)) >> 3;
  uint32_t front = 0;                                        // bit ty: tile ty over sprites

  for (int ty = VIS_TOP / 8; ty < VIS_END / 8; ty++) {
    const uint16_t idx = ty * 32 + tx;
    const uint8_t attr = video.cram[idx];
    const uint32_t *gfx = tiles[video.vram[idx] | ((attr & 0x20) << 3)];
    const uint16_t *pal = cmap_tiles[attr & 0x0F];
    const bool flip_x = attr & 0x40, flip_y = attr & 0x80;
    if (attr & TILE_PRIORITY) { front |= 1u << ty; }

    uint16_t *dst = frame_buffer + 8 * ty - VIS_TOP;
    for (int l = 0; l < 8; l++) {
      const uint8_t nx = 7 - l;                              // line l = native X tx*8 + 7 - l
      const uint32_t bits = gfx[flip_x ? 7 - nx : nx];
      for (int k = 0; k < 8; k++) {
        const int kk = flip_y ? 7 - k : k;
        dst[l * 224 + k] = pal[(bits >> (4 * kk)) & 0x0F];
      }
    }
  }

  for (int offs = 0; offs < MIKIE_SPRITE_BYTES; offs += 4) {
    blit_sprite(row, &video.sprites[offs], front);
  }
}

// MAME draw_sprites: native box X [sx, sx+16), Y [sy, sy+16), pen 0
// transparent, no wrap. Tiles flagged front hide the sprite.
void GAME_IRAM mikie::blit_sprite(short row, const uint8_t *s, uint32_t front) {
  const int x = s[3], y = 244 - (int)s[1];
  const int x_hi = 255 - 8 * (row - 2), x_lo = x_hi - 7;      // native X of this strip
  const int xa = x > x_lo ? x : x_lo;
  const int xb = (x + 16) <= x_hi ? (x + 15) : x_hi;
  if (xa > xb) { return; }
  const int ya = y > VIS_TOP ? y : VIS_TOP;
  const int yb = (y + 16) < VIS_END ? (y + 16) : VIS_END;
  if (ya >= yb) { return; }

  const uint16_t code = (s[2] & 0x3F) | ((s[2] & 0x80) >> 1) | ((s[0] & 0x40) << 1)
                        | ((s[2] & 0x40) << 2);              // bit 8: gfx bank 2
  const uint8_t (*gfx)[8] = sprite_gfx[code];
  const uint16_t *pal = cmap_sprites[s[0] & 0x0F];
  const bool flip_x = !(s[0] & 0x10), flip_y = s[0] & 0x20;

  for (int X = xa; X <= xb; X++) {
    const int i = flip_x ? 15 - (X - x) : X - x;
    uint16_t *dst = frame_buffer + (x_hi - X) * 224 - VIS_TOP;
    for (int Y = ya; Y < yb; Y++) {
      if (front & (1u << (Y >> 3))) { continue; }
      const int j = flip_y ? 15 - (Y - y) : Y - y;
      const uint8_t p = (gfx[j][i >> 1] >> ((i & 1) << 2)) & 0x0F;
      if (p) { dst[Y] = pal[p]; }
    }
  }
}

#ifdef LED_PIN
// from logos/mikie.png: purple and light blue letters, red heart
static const CRGB MIKIE_LED_A = CRGB(0x94cbec);
static const CRGB MIKIE_LED_B = CRGB(0x8061a6);
static const CRGB MIKIE_LED_C = CRGB(0x8d1d32);

void mikie::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      MIKIE_LED_C, MIKIE_LED_B, MIKIE_LED_A, MIKIE_LED_A,
      MIKIE_LED_A, MIKIE_LED_B, MIKIE_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// heart red while a button is held, else the menu colors
void mikie::gameLeds(CRGB *leds) {
  if (input->buttons_get() & (BUTTON_FIRE | BUTTON_START)) {
    fill_solid(leds, NUM_LEDS, MIKIE_LED_C);
    return;
  }
  menuLeds(leds);
}
#endif

#endif // GAME_MIKIE
