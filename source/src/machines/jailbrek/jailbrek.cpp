// ============================================================================
// machines/jailbrek/jailbrek.cpp
//
// Jail Break (Konami 1986, GX507), MAME konami/jailbrek.cpp + k005849.cpp.
// One Konami-1 6809 drives everything: 64x32 tilemap with row (or column)
// scroll, 48 sprites, the SN76489A and the VLM5030. Music tempo comes from
// 8 NMIs per frame, the game loop from the vblank IRQ.
// ============================================================================
#include "../../catalog.h"
#ifdef GAME_JAILBREK

#include "jailbrek.h"
#include "jailbrek_logo.h"
#include "../../emulation/input.h"
#include "jailbrek_rom.h"
#include "jailbrek_vlm.h"
#include "jailbrek_tilemap.h"
#include "jailbrek_spritemap.h"
#include "jailbrek_cmap.h"

#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int ACTION_KEY = BUTTON_START;
#else
static const unsigned int ACTION_KEY = BUTTON_EXTRA;
#endif

static const uint16_t ROM_BASE = 0x8000;
static const uint16_t VLM_ROM_SIZE = 0x2000;

// K005849 scanline timer: NMI on lines 16, 48, ... 240, vblank IRQ at 240
static const int NMI_LINE_MASK = 0x1f, NMI_LINE = 0x10;
static const int VBLANK_LINE = 240;

// VLM5030: 3.579545 MHz / 440 = 8136 Hz, 188.8 6809 cycles per sample
static const uint32_t VLM_HZ = 8136, AUDIO_HZ = 24000;
static const uint32_t CYCLES_PER_VLM_SAMPLE = 189;
// MAME routes SN and VLM at 1.0, as yiear
static const int32_t SPEECH_GAIN = 45, SPEECH_SHIFT = 12;

// K005849 control registers
static const uint8_t CTRL2_COLSCROLL = 0x04;
static const uint8_t CTRL3_SPR_BANK = 0x08;
static const uint8_t CTRL4_NMI = 0x01, CTRL4_IRQ = 0x02;  // b3 flip ignored (upright)
// VLM control 0x4000
static const uint8_t VLM_ST = 0x02, VLM_RST = 0x04;

// sprite_S::flags
static const uint8_t SPR_FLIPX = 0x01;  // attr b4
static const uint8_t SPR_FLIPY = 0x02;  // attr b5
static const uint8_t SPR_HI = 0x04;     // attr b6, code bit 8

// order: largest first, as the Arena plan packs them
jailbrek::jailbrek()
  : jailbrek_spritemap(jailbrek_spritemap_blob), jailbrek_rom(jailbrek_rom_blob),
    jailbrek_tilemap(jailbrek_tilemap_blob), jailbrek_vlm(jailbrek_vlm_blob) {
  sprites = jailbrek_spritemap.data();
  rom_ptr = jailbrek_rom.data();
  tiles = jailbrek_tilemap.data();
  vlm_rom = jailbrek_vlm.data();
}

void jailbrek::reset() {
  machineBase::reset();
  memset(ctrl, 0, sizeof(ctrl));
  nmi_line = irq_line = false;

  main_cycles = 0;
  vlm.reset(vlm_rom, VLM_ROM_SIZE);
  vlm_next = 0;
  speech.reset(VLM_HZ, AUDIO_HZ);

  m6809_reset(&main_cpu);
  // opcode fetches from ROM straight from rom_direct, Konami-1 decrypted by
  // the core; must follow m6809_reset(), which clears it
  main_cpu.rom_direct = rom_ptr;
  main_cpu.rom_base = ROM_BASE;
  main_cpu.rom_size = 0x10000 - ROM_BASE;
  main_cpu.konami1 = 1;
}

// ── 6809 ──

// Konami-1 decrypts every opcode fetch; ROM fetches never get here
unsigned char GAME_IRAM jailbrek::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  const uint8_t x = ((addr & 0x02) ? 0x80 : 0x20) | ((addr & 0x08) ? 0x08 : 0x02);
  return m6809_read(s, addr) ^ x;
}

unsigned char GAME_IRAM jailbrek::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= ROM_BASE) {
    return rom_ptr[addr - ROM_BASE];
  }
  if (addr < JB_MEM_END) {
    return memory[addr];
  }
  return read_io(addr);
}

uint8_t jailbrek::read_io(uint16_t addr) {
  switch (addr) {
    case 0x3100:
      return JAILBREK_DSW2 | (input->demoSoundsOff() ? JAILBREK_DSW2_DEMO_SOUND_OFF : 0);
    case 0x3200:
      return JAILBREK_DSW3;
    case 0x3300: {  // SYSTEM
      const unsigned int keys = input->buttons_get();
      uint8_t v = 0xff;
      if (keys & BUTTON_COIN) { v &= ~0x01; }
      if (keys & BUTTON_START) { v &= ~0x08; }
      return v;
    }
    case 0x3301: {  // P1: 8-way stick, BUTTON1 shoot, BUTTON2 weapon select
      const unsigned int keys = input->buttons_get();
      uint8_t v = 0xff;
      if (keys & BUTTON_LEFT) { v &= ~0x01; }
      if (keys & BUTTON_RIGHT) { v &= ~0x02; }
      if (keys & BUTTON_UP) { v &= ~0x04; }
      if (keys & BUTTON_DOWN) { v &= ~0x08; }
      if (keys & BUTTON_FIRE) { v &= ~0x10; }
      if (keys & (ACTION_KEY | BUTTON_B | BUTTON_L1 | BUTTON_R1)) { v &= ~0x20; }
      return v;
    }
    case 0x3303:
      return JAILBREK_DSW1;
    case 0x6000:
      vlm_catch_up();
      return vlm.bsy() ? 1 : 0;
    default:
      return 0xff;  // 3302 P2 (cocktail), unmapped
  }
}

void GAME_IRAM jailbrek::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr < JB_MEM_END) {
    memory[addr] = val;
    return;
  }
  if (addr >= 0x2040 && addr < 0x2048) {
    ctrl_w(addr & 7, val);
    return;
  }
  switch (addr) {
    case 0x3100: sn[0].write(val); return;
    case 0x4000:
      vlm_catch_up();
      vlm.st_w(val & VLM_ST);
      vlm.rst_w(val & VLM_RST);
      return;
    case 0x5000: vlm.data_w(val); return;
    default: return;  // ROM, 3000 coin counters, 3200, 3300 watchdog
  }
}

// ctrl4: clearing an enable bit releases its held line (MAME CLEAR_LINE)
void jailbrek::ctrl_w(uint8_t reg, uint8_t v) {
  ctrl[reg] = v;
  if (reg != 4) {
    return;
  }
  if (!(v & CTRL4_NMI)) { nmi_line = false; }
  if (!(v & CTRL4_IRQ)) {
    irq_line = false;
    main_cpu.irq_pending = 0;  // a masked request must not fire later
  }
  if (v & CTRL4_IRQ) { game_started = 1; }
}

// VLM samples up to the current 6809 cycle, so BSY polls see speech progress
void jailbrek::vlm_catch_up() {
  while ((int32_t)(main_cycles - vlm_next) >= 0) {
    speech.push(vlm.sample());
    vlm_next += CYCLES_PER_VLM_SAMPLE;
  }
}

// ── frame ──

// Main CPU idle loop (MAME dasm, same task queue code as yiear):
//   8073 LDX <$3C / LDD ,X / ASLA / BCS $8073
// waits for the task queue slot [<$3C] to lose bit 7, 44 % of the main
// CPU's instructions. The NMI and IRQ handlers fill the queue, so while PC
// is on the LDX, the slot is still empty and no IRQ can be taken, the rest
// of the line is spinning: charged in whole passes, cycle exact
// (main_cycles times the VLM).
static const uint16_t IDLE_PC = 0x8073, IDLE_END = 0x807A;  // LDX .. after BCS
static const uint8_t IDLE_PTR_DP = 0x3C;
static const uint8_t IDLE_SLOT_EMPTY = 0x80;
static const int IDLE_PASS_INSTR = 4;
static const int IDLE_PASS_CYCLES = 15;   // LDX 5 + LDD 5 + ASLA 2 + BCS 3

bool jailbrek::in_idle_loop() const {
  return main_cpu.PC > IDLE_PC && main_cpu.PC < IDLE_END;
}

bool jailbrek::main_idle() {
  if (main_cpu.PC != IDLE_PC) {
    return false;
  }
  // the core takes an IRQ only through m6809_irq(), called while irq_line
  if (irq_line && !(main_cpu.CC & M6809_CC_I)) {
    return false;
  }
  const uint16_t ptr = (main_cpu.DP << 8) | IDLE_PTR_DP;
  const uint16_t slot = (m6809_read(&main_cpu, ptr) << 8) | m6809_read(&main_cpu, ptr + 1);
  return m6809_read(&main_cpu, slot) & IDLE_SLOT_EMPTY;
}

// One frame from line 0. NMI is edge triggered: asserting a held line
// again does nothing, the handler re-arms it by clearing ctrl4 b0.
// The IRQ line is level: re-requested while held, like MAME ASSERT_LINE.
void jailbrek::run_frame(void) {
  int debt = 0;
  for (int line = 0; line < LINES; line++) {
    if (line == VBLANK_LINE) {
      publish_video();
    }
    if ((line & NMI_LINE_MASK) == NMI_LINE && (ctrl[4] & CTRL4_NMI) && !nmi_line) {
      nmi_line = true;
      m6809_nmi(&main_cpu);
    }
    if (line == VBLANK_LINE && (ctrl[4] & CTRL4_IRQ)) {
      irq_line = true;
    }

    debt += CYCLES_PER_LINE;
    while (debt > 0) {
      if (main_idle()) {
        const int spun = (debt + IDLE_PASS_CYCLES - 1) / IDLE_PASS_CYCLES * IDLE_PASS_CYCLES;
        debt -= spun;
        main_cycles += spun;
        break;
      }
      if (irq_line) { m6809_irq(&main_cpu); }  // taken once CC.I clears
      // single steps inside the loop: a 4 step can't drift onto its LDX
      const int c = m6809_step(&main_cpu, in_idle_loop() ? 1 : 4);
      const int used = c > 0 ? c : 1;
      debt -= used;
      main_cycles += used;
    }
  }
  vlm_catch_up();
}

// Latch what render reads, where MAME raises vblank.
void jailbrek::publish_video() {
  vblank.publish([this](VideoState &s) {
    const int vis = FIRST_TILE_ROW * 64;
    memcpy(s.cram, memory + JB_MEM_CRAM + vis, sizeof(s.cram));
    memcpy(s.vram, memory + JB_MEM_VRAM + vis, sizeof(s.vram));
    memcpy(s.spr, memory + ((ctrl[3] & CTRL3_SPR_BANK) ? JB_MEM_SPR2 : JB_MEM_SPR),
           sizeof(s.spr));
    memcpy(s.scroll, memory + JB_MEM_SCROLL, sizeof(s.scroll));
    memcpy(s.ctrl, ctrl, sizeof(s.ctrl));
  });
}

// MAME order: offsets ascending, later sprites win.
void jailbrek::prepare_frame(void) {
  vblank.read(video);

  active_sprites = 0;
  for (int offs = 0; offs < SPRITE_BYTES; offs += 4) {
    const uint8_t *s = video.spr + offs;
    const uint8_t attr = s[1];
    const int x = s[2] - ((attr & 0x80) << 1) - 8;
    const int y = s[3] - 16;
    if (x <= -16 || x >= OUT_WIDTH || y <= -16 || y >= VIS_LINES) { continue; }
    sprite_S &spr = sprite[active_sprites++];
    spr.x = x;
    spr.y = y;
    spr.code = s[0];
    spr.color = attr & 0x0f;
    spr.flags = ((attr >> 4) & 3) | ((attr & 0x40) ? SPR_HI : 0);  // SPR_FLIPX, SPR_FLIPY
  }
}

// 8 pens of one tile row into dst, opaque
static inline void put_tile_row(unsigned short *dst, uint32_t bits,
                                const unsigned short *cmap, int px0, int n) {
  for (int k = 0; k < n; k++) {
    dst[k] = cmap[(bits >> ((7 - (px0 + k)) * 4)) & 15];
  }
}

// Row scroll: tile row of this line scrolled by its 9 bit x value.
void jailbrek::render_tiles_rows(unsigned short *dst, int line) {
  const int nrow = (line + 16) >> 3;
  const int ty = (line + 16) & 7;
  const uint8_t *cram = video.cram + (nrow - FIRST_TILE_ROW) * 64;
  const uint8_t *vram = video.vram + (nrow - FIRST_TILE_ROW) * 64;
  const uint16_t scroll = video.scroll[nrow] | ((video.scroll[nrow + 0x20] & 1) << 8);
  uint16_t tx = (8 + scroll) & 511;
  int x = 0;
  while (x < OUT_WIDTH) {
    const int px0 = tx & 7;
    int n = 8 - px0;
    if (n > OUT_WIDTH - x) { n = OUT_WIDTH - x; }
    const int idx = tx >> 3;
    const uint8_t attr = cram[idx];
    const uint32_t bits = tiles[vram[idx] | ((attr & 0xc0) << 2)][ty];
    put_tile_row(dst + x, bits, jailbrek_tile_cmap[attr & 0x0f], px0, n);
    x += n;
    tx = (tx + n) & 511;
  }
}

// Column scroll: each 16 px group scrolled by its own y value, wraps at 256.
// Rows outside the snapshot are read live (credits only, edge garbage).
void jailbrek::render_tiles_cols(unsigned short *dst, int line) {
  for (int col = 1; col <= 30; col++) {  // native x 8..247
    const int g = col >> 1;
    const uint16_t scroll = video.scroll[g] | ((video.scroll[g + 0x20] & 1) << 8);
    const int ny = (line + 16 + scroll) & 255;
    const int nrow = ny >> 3;
    const int vis = nrow - FIRST_TILE_ROW;
    uint8_t attr, code;
    if (vis >= 0 && vis < VIS_TILE_ROWS) {
      attr = video.cram[vis * 64 + col];
      code = video.vram[vis * 64 + col];
    } else {
      attr = memory[JB_MEM_CRAM + nrow * 64 + col];
      code = memory[JB_MEM_VRAM + nrow * 64 + col];
    }
    const uint32_t bits = tiles[code | ((attr & 0xc0) << 2)][ny & 7];
    put_tile_row(dst + (col - 1) * 8, bits, jailbrek_tile_cmap[attr & 0x0f], 0, 8);
  }
}

// Opaque tiles, then sprites on top (pens with lookup nibble 0 clear).
void jailbrek::render_line(unsigned short *dst, int line) {
  if (video.ctrl[2] & CTRL2_COLSCROLL) {
    render_tiles_cols(dst, line);
  } else {
    render_tiles_rows(dst, line);
  }

  for (int i = 0; i < active_sprites; i++) {
    const sprite_S &s = sprite[i];
    int r = line - s.y;
    if (r < 0 || r > 15) { continue; }
    if (s.flags & SPR_FLIPY) { r = 15 - r; }
    const uint32_t *bits = sprites[s.code | ((s.flags & SPR_HI) << 6)][r];
    const uint16_t transp = jailbrek_spr_transp[s.color];
    if (!(bits[0] | bits[1]) && (transp & 1)) { continue; }
    const unsigned short *cmap = jailbrek_spr_cmap[s.color];
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

void jailbrek::render_row(short row) {
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

const Logo &jailbrek::logo(void) {
  return jailbrek_logo;
}

#ifdef LED_PIN
// from logos/jailbrek.png: red, orange, yellow
static const CRGB JAILBREK_LED_RED = CRGB(0xe61e1e);
static const CRGB JAILBREK_LED_ORANGE = CRGB(0xf18a0e);
static const CRGB JAILBREK_LED_YELLOW = CRGB(0xfff078);

void jailbrek::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      JAILBREK_LED_RED, JAILBREK_LED_ORANGE, JAILBREK_LED_YELLOW, JAILBREK_LED_YELLOW,
      JAILBREK_LED_YELLOW, JAILBREK_LED_ORANGE, JAILBREK_LED_RED};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// blink red while FIRE is held, else the menu colors
void jailbrek::gameLeds(CRGB *leds) {
  fireBlinkLeds(leds, input, led_frame, JAILBREK_LED_RED, menuLeds);
}
#endif

// ── audio, video core ──

void jailbrek::renderAudio(int16_t *out, int n) {
  Sn76489::renderBank(sn, out, n);
  int16_t voice[AUDIO_CHUNK];
  speech.render(voice, n);
  for (int i = 0; i < n; i++) {
    out[i] = audioClamp(out[i] + ((voice[i] * SPEECH_GAIN) >> SPEECH_SHIFT), AUDIO_MAX);
  }
}

#endif // GAME_JAILBREK
