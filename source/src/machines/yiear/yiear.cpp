// ============================================================================
// machines/yiear/yiear.cpp
//
// Yie Ar Kung-Fu (Konami 1985), MAME konami/yiear.cpp.
// One 6809 drives everything: tilemap (32x32, no scroll), 24 sprites,
// SN76489A behind the Konami latch and the VLM5030. Music tempo comes from
// an NMI every 32 lines, the game loop from the vblank IRQ.
// ============================================================================
#ifdef ENABLE_YIEAR

#include "yiear.h"
#include "../../emulation/input.h"
#include "yiear_rom.h"
#include "yiear_vlm.h"
#include "yiear_tilemap.h"
#include "yiear_spritemap.h"
#include "yiear_palette.h"

#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int ACTION_KEY = BUTTON_START;
#else
static const unsigned int ACTION_KEY = BUTTON_EXTRA;
#endif

static const uint16_t ROM_BASE = 0x8000;
static const uint16_t RAM_BASE = 0x5000;
static const uint16_t VLM_ROM_SIZE = 0x2000;

// 18.432 MHz / 12 = 1.536 MHz, 264 lines of 384 pixel clocks at 6.144 MHz
static const int LINES = 264;
static const int CYCLES_PER_LINE = 97;
static const int NMI_FIRST_LINE = 16;  // MAME "16v" timer: lines 16, 48, ... 240
static const int NMI_LINE_STEP = 32;
static const int VBLANK_LINE = 240;

// VLM5030: 3.579545 MHz / 440 = 8136 Hz, 188.8 6809 cycles per sample
static const uint32_t VLM_HZ = 8136, AUDIO_HZ = 24000;
static const uint32_t CYCLES_PER_VLM_SAMPLE = 189;
// MAME routes SN and VLM at 1.0: VLM full scale (int16 32768) = 4 SN
// channels at full volume (4 x 90 here), 360 / 32768 = 45 / 4096
static const int32_t SPEECH_GAIN = 45, SPEECH_SHIFT = 12;

// control register 0x4000
static const uint8_t CTRL_NMI = 0x02, CTRL_IRQ = 0x04;  // b0 flip, b3/b4 coin counters ignored
// VLM control 0x4a00
static const uint8_t VLM_ST = 0x02, VLM_RST = 0x04;

// sprite_S::flags
static const uint8_t SPR_FLIPX = 0x01;
static const uint8_t SPR_FLIPY = 0x02;
static const uint8_t SPR_HI = 0x04;  // code bit 8

static const int SPRITE_TOP_FIX = 0x26;  // MAME: offs below get sy + 1 (title screen)

yiear::yiear()
  : yiear_rom(yiear_rom_blob), yiear_vlm(yiear_vlm_blob),
    yiear_tilemap(yiear_tilemap_blob), yiear_spritemap(yiear_spritemap_blob) {
  rom_ptr = yiear_rom.data();
  vlm_rom = yiear_vlm.data();
  tiles = yiear_tilemap.data();
  sprites = yiear_spritemap.data();
}

void yiear::reset() {
  machineBase::reset();
  nmi_enable = irq_enable = false;
  nmi_line = irq_line = false;
  sn_latch = 0;

  main_cycles = 0;
  vlm.reset(vlm_rom, VLM_ROM_SIZE);
  vlm_next = 0;
  speech.reset(VLM_HZ, AUDIO_HZ);

  m6809_reset(&main_cpu);
  main_cpu.rom_direct = rom_ptr;  // must follow m6809_reset()
  main_cpu.rom_base = ROM_BASE;
  main_cpu.rom_size = 0x10000 - ROM_BASE;
}

// ── 6809 ──

unsigned char IRAM_ATTR yiear::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

unsigned char IRAM_ATTR yiear::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= ROM_BASE) {
    return rom_ptr[addr - ROM_BASE];
  }
  if (addr >= RAM_BASE && addr < RAM_BASE + YIEAR_MEM_END) {
    return memory[addr - RAM_BASE];
  }
  return read_io(addr);
}

uint8_t yiear::read_io(uint16_t addr) {
  switch (addr) {
    case 0x0000:
      vlm_catch_up();
      return vlm.bsy() ? 1 : 0;
    case 0x4c00:
      return YIEAR_DSW2 | (input->demoSoundsOff() ? YIEAR_DSW2_DEMO_SOUND_OFF : 0);
    case 0x4d00:
      return YIEAR_DSW3;
    case 0x4e00: {  // SYSTEM
      const unsigned int keys = input->buttons_get();
      uint8_t v = 0xff;
      if (keys & BUTTON_COIN) { v &= ~0x01; }
      if (keys & BUTTON_START) { v &= ~0x08; }
      return v;
    }
    case 0x4e01: {  // P1: 8-way stick, BUTTON1 punch, BUTTON2 kick
      const unsigned int keys = input->buttons_get();
      uint8_t v = 0xff;
      if (keys & BUTTON_LEFT) { v &= ~0x01; }
      if (keys & BUTTON_RIGHT) { v &= ~0x02; }
      if (keys & BUTTON_UP) { v &= ~0x04; }
      if (keys & BUTTON_DOWN) { v &= ~0x08; }
      if (keys & BUTTON_FIRE) { v &= ~0x10; }
      if (keys & (ACTION_KEY | BUTTON_B)) { v &= ~0x20; }
      return v;
    }
    case 0x4e03:
      return YIEAR_DSW1;
    default:
      return 0xff;  // 0x4e02 P2 (cocktail), unmapped
  }
}

void IRAM_ATTR yiear::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr >= RAM_BASE && addr < RAM_BASE + YIEAR_MEM_END) {
    memory[addr - RAM_BASE] = val;
    return;
  }
  switch (addr) {
    case 0x4000: control_w(val); return;
    case 0x4800: sn_latch = val; return;
    case 0x4900: sn[0].write(sn_latch); return;
    case 0x4a00:
      vlm_catch_up();
      vlm.st_w(val & VLM_ST);
      vlm.rst_w(val & VLM_RST);
      return;
    case 0x4b00: vlm.data_w(val); return;
    default: return;  // ROM, 0x4f00 watchdog
  }
}

// clearing an enable bit releases its held line (MAME CLEAR_LINE)
void yiear::control_w(uint8_t v) {
  nmi_enable = v & CTRL_NMI;
  if (!nmi_enable) { nmi_line = false; }
  irq_enable = v & CTRL_IRQ;
  if (!irq_enable) { irq_line = false; }
  if (irq_enable) { game_started = 1; }
}

// VLM samples up to the current 6809 cycle, so BSY polls see speech progress
void yiear::vlm_catch_up() {
  while ((int32_t)(main_cycles - vlm_next) >= 0) {
    speech.push(vlm.sample());
    vlm_next += CYCLES_PER_VLM_SAMPLE;
  }
}

// ── frame ──

// One frame from line 0: NMI (edge) every 32 lines, vblank IRQ at line 240.
// The IRQ line is level: re-requested while held, like MAME ASSERT_LINE.
void yiear::run_frame(void) {
  int debt = 0;
  for (int line = 0; line < LINES; line++) {
    if (line == VBLANK_LINE) {
      publish_video();
      if (irq_enable) { irq_line = true; }
    }
    if (line >= NMI_FIRST_LINE && (line - NMI_FIRST_LINE) % NMI_LINE_STEP == 0 &&
        nmi_enable && !nmi_line) {
      nmi_line = true;
      m6809_nmi(&main_cpu);
    }

    debt += CYCLES_PER_LINE;
    while (debt > 0) {
      if (irq_line) { m6809_irq(&main_cpu); }  // taken once CC.I clears
      const int c = m6809_step(&main_cpu, 4);
      const int used = c > 0 ? c : 1;
      debt -= used;
      main_cycles += used;
    }
  }
  vlm_catch_up();
}

// Latch what render reads, where MAME raises vblank.
void yiear::publish_video() {
  vblank.publish([this](VideoState &s) {
    memcpy(s.vram, memory + YIEAR_MEM_VRAM + FIRST_TILE_ROW * 64, sizeof(s.vram));
    memcpy(s.spr0, memory + YIEAR_MEM_SPR0, sizeof(s.spr0));
    memcpy(s.spr1, memory + YIEAR_MEM_SPR1, sizeof(s.spr1));
  });
}

// MAME draws from the last sprite to the first: later entries end on top.
void yiear::prepare_frame(void) {
  vblank.read(video);

  active_sprites = 0;
  for (int offs = SPRITE_BYTES - 2; offs >= 0; offs -= 2) {
    const uint8_t attr = video.spr0[offs];
    int y = 240 - video.spr0[offs + 1] - 16;
    if (offs < SPRITE_TOP_FIX) { y++; }
    const int x = video.spr1[offs] - 8;
    if (x <= -16 || x >= OUT_WIDTH || y <= -16 || y >= VIS_LINES) { continue; }

    sprite_S &spr = sprite[active_sprites++];
    spr.x = x;
    spr.y = y;
    spr.code = video.spr1[offs + 1];
    spr.flags = ((attr & 0x40) ? 0 : SPR_FLIPX) | ((attr & 0x80) ? SPR_FLIPY : 0) |
                ((attr & 0x01) ? SPR_HI : 0);
  }
}

// Opaque tiles (palette 16-31), then sprites (palette 0-15, pen 0 clear).
void yiear::render_line(unsigned short *dst, int line) {
  const int ny = line + FIRST_TILE_ROW * 8;
  const int ty = ny & 7;
  const uint8_t *vram = video.vram + ((ny >> 3) - FIRST_TILE_ROW) * 64;

  // native x 8..247: tile columns 1..30
  for (int col = 1; col <= 30; col++) {
    const uint8_t attr = vram[col * 2];
    const uint16_t code = vram[col * 2 + 1] | ((attr & 0x10) << 4);
    const uint32_t bits = tiles[code][(attr & 0x40) ? 7 - ty : ty];
    unsigned short *d = dst + (col - 1) * 8;
    const bool flipx = attr & 0x80;
    for (int px = 0; px < 8; px++) {
      const int sx = flipx ? 7 - px : px;
      d[px] = yiear_palette[16 + ((bits >> ((7 - sx) * 4)) & 15)];
    }
  }

  for (int i = 0; i < active_sprites; i++) {
    const sprite_S &s = sprite[i];
    int r = line - s.y;
    if (r < 0 || r > 15) { continue; }
    if (s.flags & SPR_FLIPY) { r = 15 - r; }
    const uint32_t *bits = sprites[s.code | ((s.flags & SPR_HI) << 6)][r];
    if (!(bits[0] | bits[1])) { continue; }
    const bool flipx = s.flags & SPR_FLIPX;
    for (int c = 0; c < 16; c++) {
      const int px = s.x + c;
      if (px < 0 || px >= OUT_WIDTH) { continue; }
      const int sc = flipx ? 15 - c : c;
      const uint8_t pen = (bits[sc >> 3] >> ((7 - (sc & 7)) * 4)) & 15;
      if (pen) { dst[px] = yiear_palette[pen]; }
    }
  }
}

void yiear::render_row(short row) {
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

void yiear::renderAudio(int16_t *out, int n) {
  Sn76489::renderBank(sn, out, n);
  int16_t voice[AUDIO_CHUNK];
  speech.render(voice, n);
  for (int i = 0; i < n; i++) {
    out[i] = audioClamp(out[i] + ((voice[i] * SPEECH_GAIN) >> SPEECH_SHIFT), AUDIO_MAX);
  }
}

#ifdef LED_PIN
// logo colors (quantized logos/yiear.png): red, yellow
static const CRGB YIEAR_LED_RED(0xde0000), YIEAR_LED_ORANGE(0xee7f00),
                  YIEAR_LED_YELLOW(0xffff00);

void yiear::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      YIEAR_LED_RED, YIEAR_LED_RED, YIEAR_LED_ORANGE, YIEAR_LED_YELLOW,
      YIEAR_LED_ORANGE, YIEAR_LED_RED, YIEAR_LED_RED};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// yellow while the VLM5030 speaks (fighters' shouts), else the menu colors
void yiear::gameLeds(CRGB *leds) {
  if (vlm.bsy()) {
    fill_solid(leds, NUM_LEDS, YIEAR_LED_YELLOW);
    return;
  }
  menuLeds(leds);
}
#endif

#endif // ENABLE_YIEAR
