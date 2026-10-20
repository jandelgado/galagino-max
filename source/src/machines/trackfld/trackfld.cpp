// ============================================================================
// machines/trackfld/trackfld.cpp
//
// Track & Field (Konami 1983), MAME konami/trackfld.cpp, trackfld_a.cpp.
// Main KONAMI-1 6809: 64x32 tilemap with per-row scroll, 32 sprites, vblank
// IRQ. Sound board Z80 (IRQ from main latch Q1): SN76489A, 8-bit DAC, a
// cycle timer and the VLM5030, whose ST/RST pins come from address bits.
// Both CPUs and the VLM share one timeline in 6809 cycles.
// ============================================================================
#include "../../catalog.h"
#ifdef GAME_TRACKFLD

#include "trackfld.h"
#include "../../emulation/input.h"
#include "trackfld_rom_main.h"
#include "trackfld_rom_sound.h"
#include "trackfld_vlm.h"
#include "trackfld_tilemap.h"
#include "trackfld_spritemap.h"
#include "trackfld_cmap.h"

static const uint16_t ROM_BASE = 0x6000;
static const uint16_t SOUND_ROM_SIZE = 0x2000;
static const uint16_t VLM_ROM_SIZE = 0x2000;
static const uint16_t TILE_COUNT = 768;  // MAME wraps larger codes

// 264 lines of 384 pixel clocks at 6.144 MHz
static const int LINES = 264;
static const int CYCLES_PER_LINE = 97;    // 1.536 MHz / 60.6 / 264
static const int Z80_STEPS_PER_LINE = 20; // 3.58 MHz, tp84 / mikie calibration
static const int VBLANK_LINE = 240;

// timer 0x8000: Z80 cycles / 1024, 4 bits; Z80 cycles from the 6809 timeline
static const uint64_t Z80_HZ = 3579545, MAIN_HZ = 1536000;
static const uint32_t TIMER_DIV = 1024;

// DAC sampled at the audio rate: 400 samples per 264 lines
static const uint16_t DAC_PER_FRAME = 400;

// VLM5030 3.579545 MHz / 440 = 8136 Hz: 188.8 6809 cycles per sample
static const uint32_t VLM_HZ = 8136, AUDIO_HZ = 24000;
static const uint32_t CYCLES_PER_VLM_SAMPLE = 189;

// MAME routes: SN 1.0, VLM 1.0, DAC 0.4. Full scale 1.0 = 4 SN channels at
// full volume (4 x 90 here): int16 x 360 / 32768 = 45 / 4096, DAC x 0.4
static const int32_t SPEECH_GAIN = 45, DAC_GAIN = 18, GAIN_SHIFT = 12;

// LS259 at 0x1080: Q1 sound IRQ, Q7 vblank IRQ mask (Q0 flip, Q3/Q4 coin counters ignored)
static const uint8_t LATCH_SOUND_IRQ = 1, LATCH_IRQ_MASK = 7;

// Z80 0xe003: VLM pins from address lines A8 (ST) and A9 (RST)
static const uint16_t VLM_ST_LINE = 0x0100, VLM_RST_LINE = 0x0200;
static const uint8_t VLM_BSY = 0x10;  // 0xe002

// sprite_S::flags
static const uint8_t SPR_FLIPX = 0x01;
static const uint8_t SPR_FLIPY = 0x02;

trackfld::trackfld()
  : trackfld_rom_main(trackfld_rom_main_blob), trackfld_rom_sound(trackfld_rom_sound_blob),
    trackfld_vlm(trackfld_vlm_blob), trackfld_tilemap(trackfld_tilemap_blob),
    trackfld_spritemap(trackfld_spritemap_blob) {
  main_rom = trackfld_rom_main.data();
  sound_rom = trackfld_rom_sound.data();
  vlm_rom = trackfld_vlm.data();
  tiles = trackfld_tilemap.data();
  sprites = trackfld_spritemap.data();
}

void trackfld::reset() {
  machineBase::reset();
  irq_mask = irq_line = false;
  sound_q = snd_irq = false;
  soundlatch = sn_latch = 0;

  main_cycles = 0;
  dac = 0x80;
  dac_acc = 0;
  dac_ring.reset(AUDIO_HZ, AUDIO_HZ);
  vlm.reset(vlm_rom, VLM_ROM_SIZE);
  vlm_next = 0;
  speech.reset(VLM_HZ, AUDIO_HZ);

  m6809_reset(&main_cpu);
  // opcode fetches straight from ROM, decrypted by the core; must follow m6809_reset()
  main_cpu.rom_direct = main_rom;
  main_cpu.rom_base = ROM_BASE;
  main_cpu.rom_size = 0x10000 - ROM_BASE;
  main_cpu.konami1 = 1;
}

// ── main 6809 ──

// KONAMI-1: opcode XOR by address bits 1 and 3, ROM fetches only
unsigned char GAME_IRAM trackfld::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  if (addr >= ROM_BASE) {
    const uint8_t mask = ((addr & 0x02) ? 0x80 : 0x20) | ((addr & 0x08) ? 0x08 : 0x02);
    return main_rom[addr - ROM_BASE] ^ mask;
  }
  return m6809_read(s, addr);
}

unsigned char GAME_IRAM trackfld::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= ROM_BASE) {
    return main_rom[addr - ROM_BASE];
  }
  if (addr >= 0x3000 && addr < 0x4000) {
    return memory[TF_MEM_VRAM + (addr - 0x3000)];
  }
  if (addr >= 0x2800 && addr < 0x3000) {
    return memory[TF_MEM_NVRAM + (addr - 0x2800)];
  }
  if (addr >= 0x1800 && addr < 0x2000) {
    return memory[TF_MEM_RAM + (addr - 0x1800)];
  }
  return read_io(addr);
}

uint8_t trackfld::read_io(uint16_t addr) {
  if ((addr & 0xff80) == 0x1200) {
    return TRACKFLD_DSW2 | (input->demoSoundsOff() ? TRACKFLD_DSW2_DEMO_SOUND_OFF : 0);
  }
  if ((addr & 0xff80) != 0x1280) {
    return 0xff;
  }

  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xff;
  switch (addr & 3) {
    case 0:  // SYSTEM
      if (keys & BUTTON_COIN) { v &= ~0x01; }
      if (keys & BUTTON_START) { v &= ~0x08; }
      return v;
    case 1:  // IN0: run left B1 (b2), jump B2 (b1), run right B3 (b0)
      if (keys & (BUTTON_LEFT | BUTTON_A)) { v &= ~0x04; }
      if (keys & BUTTON_FIRE) { v &= ~0x02; }
      if (keys & (BUTTON_RIGHT | BUTTON_B)) { v &= ~0x01; }
      return v;
    case 2:  // IN1: players 3 and 4
      return v;
    default:
      return TRACKFLD_DSW1;
  }
}

void GAME_IRAM trackfld::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr >= 0x3000 && addr < 0x4000) {
    memory[TF_MEM_VRAM + (addr - 0x3000)] = val;
    return;
  }
  if (addr >= 0x2800 && addr < 0x3000) {
    memory[TF_MEM_NVRAM + (addr - 0x2800)] = val;
    return;
  }
  if (addr >= 0x1800 && addr < 0x2000) {
    memory[TF_MEM_RAM + (addr - 0x1800)] = val;
    return;
  }
  if ((addr & 0xff80) == 0x1080) {
    latch_w(addr & 7, val & 1);
    return;
  }
  if ((addr & 0xff80) == 0x1100) {
    soundlatch = val;
  }
  // 0x1000 watchdog, ROM
}

void trackfld::latch_w(uint8_t bit, bool q) {
  if (bit == LATCH_SOUND_IRQ) {
    if (q && !sound_q) { snd_irq = true; }
    sound_q = q;
    return;
  }
  if (bit != LATCH_IRQ_MASK) {
    return;
  }
  irq_mask = q;
  if (!irq_mask) { irq_line = false; }
  if (irq_mask) { game_started = 1; }
}

// ── sound Z80 ──

unsigned char trackfld::opZ80(unsigned short Addr) {
  return Addr < SOUND_ROM_SIZE ? sound_rom[Addr] : rdZ80(Addr);
}

unsigned char trackfld::rdZ80(unsigned short Addr) {
  if (Addr < SOUND_ROM_SIZE) {
    return sound_rom[Addr];
  }
  switch (Addr & 0xe000) {
    case 0x4000:
      return memory[TF_MEM_SND + (Addr & 0x3ff)];
    case 0x6000:
      return soundlatch;
    case 0x8000:
      return (uint8_t)((main_cycles * Z80_HZ / MAIN_HZ / TIMER_DIV) & 0x0f);
    case 0xc000:
      sn[0].write(sn_latch);  // MAME: a read strobes the SN too
      return 0xff;
    case 0xe000:
      if ((Addr & 7) == 2) {
        vlm_catch_up();
        return vlm.bsy() ? VLM_BSY : 0;
      }
      return 0xff;
    default:
      return 0;  // empty ROM 0x2000-0x3fff
  }
}

void trackfld::wrZ80(unsigned short Addr, unsigned char Value) {
  switch (Addr & 0xe000) {
    case 0x4000:
      memory[TF_MEM_SND + (Addr & 0x3ff)] = Value;
      return;
    case 0xa000:
      sn_latch = Value;
      return;
    case 0xc000:
      sn[0].write(sn_latch);
      return;
    case 0xe000:
      break;
    default:
      return;
  }

  switch (Addr & 7) {
    case 0:
      dac = Value;
      return;
    case 3:
      vlm_catch_up();
      vlm.st_w(Addr & VLM_ST_LINE);
      vlm.rst_w(Addr & VLM_RST_LINE);
      return;
    case 4:
      vlm.data_w(Value);
      return;
    default:
      return;  // e001 watchdog?
  }
}

// ── frame ──

// VLM samples up to the current cycle, so BSY polls see speech progress
void trackfld::vlm_catch_up() {
  while ((int32_t)(main_cycles - vlm_next) >= 0) {
    speech.push(vlm.sample());
    vlm_next += CYCLES_PER_VLM_SAMPLE;
  }
}

void trackfld::dac_tick() {
  dac_acc += DAC_PER_FRAME;
  while (dac_acc >= LINES) {
    dac_acc -= LINES;
    dac_ring.push((int16_t)((dac - 0x80) << 8));
  }
}

// Per line: 6809 by cycles, Z80 by steps, DAC samples. Vblank IRQ at line
// 240, level: re-requested while held, like MAME ASSERT_LINE.
void trackfld::run_frame(void) {
  int debt = 0;
  for (int line = 0; line < LINES; line++) {
    if (line == VBLANK_LINE) {
      publish_video();
      if (irq_mask) { irq_line = true; }
    }

    debt += CYCLES_PER_LINE;
    while (debt > 0) {
      if (irq_line) { m6809_irq(&main_cpu); }  // taken once CC.I clears
      const int c = m6809_step(&main_cpu, 4);
      const int used = c > 0 ? c : 1;
      debt -= used;
      main_cycles += used;
    }

    for (int i = 0; i < Z80_STEPS_PER_LINE; i++) {
      if (snd_irq && (cpu[0].IFF & IFF_1)) {
        IntZ80(&cpu[0], INT_RST38);
        snd_irq = false;
      }
      StepZ80(&cpu[0]);
    }
    dac_tick();
  }
  vlm_catch_up();
}

// Latch what render reads, where MAME raises vblank.
void trackfld::publish_video() {
  vblank.publish([this](VideoState &s) {
    const int vis = FIRST_TILE_ROW * 64;
    memcpy(s.vram, memory + TF_MEM_VRAM + vis, sizeof(s.vram));
    memcpy(s.cram, memory + TF_MEM_CRAM + vis, sizeof(s.cram));
    for (int r = 0; r < VIS_TILE_ROWS; r++) {
      const int row = r + FIRST_TILE_ROW;
      s.scroll[r] = memory[TF_MEM_SCRL + row] | ((memory[TF_MEM_SCRH + row] & 1) << 8);
    }
    memcpy(s.spr, memory + TF_MEM_SPR, sizeof(s.spr));
    memcpy(s.spr2, memory + TF_MEM_SPR2, sizeof(s.spr2));
  });
}

void trackfld::add_sprite(int x, int y, uint8_t code, uint8_t attr) {
  if (x <= -16 || x >= OUT_WIDTH) { return; }
  sprite_S &spr = sprite[active_sprites++];
  spr.x = x;
  spr.y = y;
  spr.code = code;
  spr.color = attr & 0x0f;
  spr.flags = ((attr & 0x40) ? 0 : SPR_FLIPX) | ((attr & 0x80) ? SPR_FLIPY : 0);
}

// MAME draws from the last sprite to the first (later entries on top), each
// also 256 px to the left for the wrap.
void trackfld::prepare_frame(void) {
  vblank.read(video);

  active_sprites = 0;
  for (int offs = SPRITE_BYTES - 2; offs >= 0; offs -= 2) {
    const uint8_t attr = video.spr2[offs];
    const int y = 240 - video.spr2[offs + 1] + 1 - 16;
    if (y <= -16 || y >= VIS_LINES) { continue; }
    const int x = video.spr[offs] - 1 - 8;
    add_sprite(x, y, video.spr[offs + 1], attr);
    add_sprite(x - 256, y, video.spr[offs + 1], attr);
  }
}

// Opaque tiles with row scroll, then sprites (pens with lookup 0 clear).
void trackfld::render_line(unsigned short *dst, int line) {
  const int ny = line + FIRST_TILE_ROW * 8;
  const int ty = ny & 7;
  const int trow = (ny >> 3) - FIRST_TILE_ROW;
  const uint8_t *vram = video.vram + trow * 64;
  const uint8_t *cram = video.cram + trow * 64;
  uint16_t tx = (uint16_t)((8 + video.scroll[trow]) & 511);
  int x = 0;
  while (x < OUT_WIDTH) {
    const int px0 = tx & 7;
    int n = 8 - px0;
    if (n > OUT_WIDTH - x) { n = OUT_WIDTH - x; }
    const int idx = tx >> 3;
    const uint8_t attr = cram[idx];
    const uint16_t code = (vram[idx] | ((attr & 0xc0) << 2)) % TILE_COUNT;
    const uint32_t bits = tiles[code][(attr & 0x20) ? 7 - ty : ty];
    const unsigned short *cmap = trackfld_tile_cmap[attr & 0x0f];
    const bool flipx = attr & 0x10;
    for (int k = 0; k < n; k++) {
      const int px = flipx ? 7 - (px0 + k) : px0 + k;
      dst[x + k] = cmap[(bits >> ((7 - px) * 4)) & 15];
    }
    x += n;
    tx = (tx + n) & 511;
  }

  for (int i = 0; i < active_sprites; i++) {
    const sprite_S &s = sprite[i];
    int r = line - s.y;
    if (r < 0 || r > 15) { continue; }
    if (s.flags & SPR_FLIPY) { r = 15 - r; }
    const uint32_t *bits = sprites[s.code][r];
    const uint16_t transp = trackfld_spr_transp[s.color];
    if (!(bits[0] | bits[1]) && (transp & 1)) { continue; }
    const unsigned short *cmap = trackfld_spr_cmap[s.color];
    const bool flipx = s.flags & SPR_FLIPX;
    for (int c = 0; c < 16; c++) {
      const int px = s.x + c;
      if (px < 0 || px >= OUT_WIDTH) { continue; }
      const int sc = flipx ? 15 - c : c;
      const uint8_t pen = (bits[sc >> 3] >> ((7 - (sc & 7)) * 4)) & 15;
      if (!((transp >> pen) & 1)) { dst[px] = cmap[pen]; }
    }
  }
}

void trackfld::render_row(short row) {
  for (int sr = 0; sr < 8; sr++) {
    unsigned short *dst = frame_buffer + sr * OUT_WIDTH;
    const int line = row * 8 + sr - 32;
    if (line < 0 || line >= VIS_LINES) {
      memset(dst, 0, OUT_WIDTH * sizeof(unsigned short));
      continue;
    }
    render_line(dst, line);
  }
}

// ── audio, video core ──

void trackfld::renderAudio(int16_t *out, int n) {
  Sn76489::renderBank(sn, out, n);
  int16_t voice[AUDIO_CHUNK], dacs[AUDIO_CHUNK];
  speech.render(voice, n);
  dac_ring.render(dacs, n);
  for (int i = 0; i < n; i++) {
    const int32_t d = dac_dc.filter((dacs[i] * DAC_GAIN) >> GAIN_SHIFT);
    const int32_t v = (voice[i] * SPEECH_GAIN) >> GAIN_SHIFT;
    out[i] = audioClamp(out[i] + d + v, AUDIO_MAX);
  }
}

#ifdef LED_PIN
// logo colors (quantized logos/trackfld.png): blue, yellow, white, red
static const CRGB TRACKFLD_LED_BLUE(0x0000fb), TRACKFLD_LED_YELLOW(0xffff00),
                  TRACKFLD_LED_WHITE(0xfffffb), TRACKFLD_LED_RED(0xff0000);

void trackfld::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      TRACKFLD_LED_BLUE, TRACKFLD_LED_BLUE, TRACKFLD_LED_WHITE, TRACKFLD_LED_RED,
      TRACKFLD_LED_WHITE, TRACKFLD_LED_BLUE, TRACKFLD_LED_BLUE};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// yellow while the VLM5030 announcer speaks, else the menu colors
void trackfld::gameLeds(CRGB *leds) {
  if (vlm.bsy()) {
    fill_solid(leds, NUM_LEDS, TRACKFLD_LED_YELLOW);
    return;
  }
  menuLeds(leds);
}
#endif

#endif // GAME_TRACKFLD
