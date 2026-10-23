#include "../../catalog.h"
#ifdef GAME_JUNOFRST

#include "junofrst.h"
#include "junofrst_rom.h"
#include "junofrst_bank.h"
#include "junofrst_snd.h"
#include "junofrst_i8039.h"
#include "junofrst_blit.h"
#include "junofrst_pens.h"

static constexpr uint16_t MAIN_ROM_BASE = 0xa000;

// one frame = 256 lines: main 1.5 MHz, Z80 1.79 MHz, i8039 8 MHz / 15
static constexpr int LINES = 256;
static constexpr int32_t MAIN_CYCLES_FRAME = 1500000 / 60;
static constexpr int Z80_STEPS_LINE = 10;      // ~117 cycles
static constexpr int Z80_CYCLES_LINE = 117;
static constexpr int32_t I8039_HZ = 8000000 / 15;
static constexpr int I8039_SLICE = 16;         // lines per i8039 run

// inputs, active low (MAME KONAMI8_SYSTEM_10 / KONAMI8_MONO_B213)
static constexpr uint8_t SYS_COIN1 = 0x01, SYS_START1 = 0x08;
static constexpr uint8_t P1_LEFT = 0x01, P1_RIGHT = 0x02, P1_UP = 0x04, P1_DOWN = 0x08;
static constexpr uint8_t P1_WARP = 0x10, P1_FIRE = 0x20;

// warp on the START button: BUTTON_START with a COIN pin (EXTRA is then
// COIN) or the galagino controller, else START itself reports as
// BUTTON_EXTRA (BUTTON_START only follows a virtual coin), as contra
#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int WARP_KEY = BUTTON_START;
#else
static const unsigned int WARP_KEY = BUTTON_EXTRA;
#endif

// LS259 at 8030-8037
static constexpr uint8_t LATCH_IRQ_ENABLE = 0;   // others: coin counters, flip (upright: ignored)

static constexpr uint8_t AY_PORT_A = 14;
static constexpr uint8_t I8039_INT_ENABLE = 0x80;   // P2 b7 = 0 clears INT

junofrst::junofrst()
  : junofrst_rom(junofrst_rom_blob), junofrst_bank(junofrst_bank_blob),
    junofrst_snd(junofrst_snd_blob), junofrst_i8039(junofrst_i8039_blob),
    junofrst_blit(junofrst_blit_blob), junofrst_pens(junofrst_pens_blob) {
  // m6809_reset() reads the reset vector through main_rom
  main_rom = junofrst_rom.data();
  bank_rom = junofrst_bank.data();
  sound_rom = junofrst_snd.data();
  i8039_rom = junofrst_i8039.data();
  blit_rom = junofrst_blit.data();
  pen_rgb = junofrst_pens.data();
}

void junofrst::reset() {
  machineBase::reset();   // clears memory[], resets the Z80
  videoram = nullptr;     // Arena memory, reclaimed by Arena::reset()
  irq_enable = irq_toggle = 0;
  bank = 0;
  memset(blitter, 0, sizeof(blitter));
  sound_latch = sound_latch2 = 0;
  snd_irq_last = snd_irq = 0;
  ay_addr = 0;
  i8039_status = 0;
  snd_cycles = 0;
  memset(snd_ram, 0, sizeof(snd_ram));
  memset(&video, 0, sizeof(video));
  memset(pens, 0, sizeof(pens));

  m6809_reset(&main_cpu);
  main_cpu.rom_direct = main_rom;   // must follow m6809_reset()
  main_cpu.rom_base = MAIN_ROM_BASE;
  main_cpu.rom_size = 0x10000 - MAIN_ROM_BASE;
  main_cpu.konami1 = 1;

  dac.reset(i8039_rom);
}

void junofrst::start() {
  // start() runs twice per session; the Arena has no free, so allocate once
  if (!videoram) {
    videoram = Arena::alloc(VRAM_SIZE);
    memset(videoram, 0, VRAM_SIZE);
  }
}

// ---- Main KONAMI-1 ----

// opcodes are encrypted at every address, the bank window included
unsigned char GAME_IRAM junofrst::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  const uint8_t mask = ((addr & 0x02) ? 0x80 : 0x20) | ((addr & 0x08) ? 0x08 : 0x02);
  return junofrst::m6809_read(s, addr) ^ mask;
}

unsigned char GAME_IRAM junofrst::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= MAIN_ROM_BASE) {
    return main_rom[addr - MAIN_ROM_BASE];
  }
  if (addr < VRAM_SIZE) {
    return videoram[addr];
  }
  if (addr >= 0x9000) {
    return bank < BANKS ? bank_rom[bank * 0x1000 + (addr - 0x9000)] : 0xff;
  }
  if (addr >= 0x8100) {
    return memory[JUNOFRST_WORKRAM + (addr - 0x8100)];
  }
  if (addr < 0x8010) {
    return memory[JUNOFRST_PALETTE + (addr & 0x0f)];
  }
  return readInputs(addr);
}

uint8_t junofrst::readInputs(uint16_t addr) {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xff;
  switch (addr) {
    case 0x8010:
      return JUNOFRST_DSW2 | (input->demoSoundsOff() ? JUNOFRST_DSW2_DEMO_SOUND_OFF : 0);
    case 0x8020:
      if (keys & BUTTON_COIN) { v &= ~SYS_COIN1; }
      if (keys & BUTTON_START) { v &= ~SYS_START1; }
      return v;
    case 0x8024:
      if (keys & BUTTON_LEFT) { v &= ~P1_LEFT; }
      if (keys & BUTTON_RIGHT) { v &= ~P1_RIGHT; }
      if (keys & BUTTON_UP) { v &= ~P1_UP; }
      if (keys & BUTTON_DOWN) { v &= ~P1_DOWN; }
      if (keys & (BUTTON_FIRE | BUTTON_A)) { v &= ~P1_FIRE; }
      if (keys & (WARP_KEY | BUTTON_B | BUTTON_R1)) { v &= ~P1_WARP; }
      return v;
    case 0x802c:
      return JUNOFRST_DSW1;
  }
  return 0xff;   // 801c watchdog, 8028 player 2, unmapped
}

void GAME_IRAM junofrst::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr < VRAM_SIZE) {
    videoram[addr] = val;
    return;
  }
  if (addr >= 0x8100 && addr < 0x9000) {
    memory[JUNOFRST_WORKRAM + (addr - 0x8100)] = val;
    return;
  }
  if (addr < 0x8010) {
    memory[JUNOFRST_PALETTE + (addr & 0x0f)] = val;
    return;
  }
  if (addr >= 0x8030 && addr < 0x8038) {
    latchWrite(addr & 0x07, val & 1);
    return;
  }
  if (addr >= 0x8070 && addr < 0x8074) {
    blitter[addr & 0x03] = val;
    if (addr == 0x8073) {
      blit();
    }
    return;
  }
  switch (addr) {
    case 0x8040:   // sound IRQ on the rising edge of bit 0 (MAME HOLD_LINE)
      if (!snd_irq_last && val == 1) {
        snd_irq = 1;
      }
      snd_irq_last = val;
      return;
    case 0x8050:
      sound_latch = val;
      return;
    case 0x8060:
      bank = val & 0x0f;
      return;
  }
}

void junofrst::latchWrite(uint8_t bit, uint8_t state) {
  if (bit != LATCH_IRQ_ENABLE) {
    return;
  }
  irq_enable = state;
  if (!state) {
    main_cpu.irq_pending = 0;   // CLEAR_LINE
  }
  if (state) {
    game_started = 1;   // boot done, attract mode starts
  }
}

// 16 x 16 nibbles from the blitter ROM into the bitmap (MAME blitter_w):
// nibble addresses, source bit 0 selects copy (1) or erase (0) of the
// pixels set in the source; zero source pixels leave the bitmap alone
void junofrst::blit(void) {
  uint16_t dest = (blitter[0] << 8) | blitter[1];
  uint32_t src = ((blitter[2] << 8) | blitter[3]) & 0xfffc;   // past the ROM: blank
  const bool copy = blitter[3] & 0x01;

  for (int y = 0; y < 16; y++) {
    for (int x = 0; x < 16; x++, src++, dest++) {
      const uint32_t byte = src >> 1;
      if (byte >= BLIT_SIZE) {
        continue;
      }
      uint8_t pixel = (src & 1) ? blit_rom[byte] & 0x0f : blit_rom[byte] >> 4;
      if (!pixel) {
        continue;
      }
      if (!copy) {
        pixel = 0;
      }

      uint8_t &cell = videoram[dest >> 1];
      cell = (dest & 1) ? (cell & 0x0f) | (pixel << 4) : (cell & 0xf0) | pixel;
    }
    dest += 240;
  }
}

// ---- Sound Z80 ----

unsigned char junofrst::opZ80(unsigned short Addr) {
  return Addr < 0x1000 ? sound_rom[Addr] : 0xff;
}

unsigned char junofrst::rdZ80(unsigned short Addr) {
  if (Addr < 0x1000) {
    return sound_rom[Addr];
  }
  if ((Addr & 0xfc00) == 0x2000) {
    return snd_ram[Addr & 0x3ff];
  }
  if (Addr == 0x3000) {
    return sound_latch;
  }
  if (Addr == 0x4001) {
    return ay_addr == AY_PORT_A ? ayPortA() : soundregs[ay_addr];
  }
  return 0xff;
}

void junofrst::wrZ80(unsigned short Addr, unsigned char Value) {
  if ((Addr & 0xfc00) == 0x2000) {
    snd_ram[Addr & 0x3ff] = Value;
    return;
  }
  switch (Addr) {
    case 0x4000:
      ay_addr = Value & 0x0f;
      return;
    case 0x4002:
      soundregs[ay_addr] = Value;   // port B: RC filter caps, ignored
      return;
    case 0x5000:
      sound_latch2 = Value;
      return;
    case 0x6000:
      dac.irq(true);   // ASSERT_LINE until P2 b7 = 0
      return;
  }
}

// MAME portA_r: Z80 clock / 1024 (cycle counter at half rate), b4-7, plus
// the i8039 status in b0-2
uint8_t junofrst::ayPortA(void) {
  return (uint8_t)((((snd_cycles / 512) & 0x0f) << 4) | i8039_status);
}

// ---- i8039: P1 = DAC, P2 b7 INT clear / b4-6 status, any MOVX = latch2 ----

void junofrst::wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char val) {
  if (port == 1) {
    dac.port1(val);
    return;
  }
  if (port != 2) {
    return;
  }
  if (!(val & I8039_INT_ENABLE)) {
    dac.irq(false);
  }
  i8039_status = (val & 0x70) >> 4;
}

unsigned char junofrst::rdI8048_port(struct i8048_state_S *state, unsigned char port) {
  return 0xff;
}

unsigned char junofrst::rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) {
  return sound_latch2;
}

unsigned char junofrst::rdI8048_rom(struct i8048_state_S *state, unsigned short addr) {
  return i8039_rom[addr & 0x0fff];
}

// ---- Frame ----

// vblank: latch the palette, IRQ to the main CPU every 2nd frame (MAME
// _30hz_irq), then 256 lines of both CPUs; the i8039 runs along in
// slices of 16 lines
void junofrst::run_frame(void) {
  vblank.publish([this](VideoState &v) {
    memcpy(v.palette, &memory[JUNOFRST_PALETTE], sizeof(v.palette));
  });
  irq_toggle ^= 1;
  if (irq_toggle && irq_enable) {
    m6809_irq(&main_cpu);
  }

  int32_t main_debt = 0;
  for (int line = 0; line < LINES; line++) {
    main_debt += MAIN_CYCLES_FRAME * (line + 1) / LINES - MAIN_CYCLES_FRAME * line / LINES;
    while (main_debt > 0) {
      const int c = m6809_step(&main_cpu, 4);
      main_debt -= c > 0 ? c : 1;
    }

    // HOLD_LINE: held until the Z80 takes it
    if (snd_irq && (cpu[0].IFF & IFF_1)) {
      IntZ80(&cpu[0], INT_RST38);
      snd_irq = 0;
    }
    for (int i = 0; i < Z80_STEPS_LINE; i++) {
      StepZ80(&cpu[0]);
    }
    snd_cycles += Z80_CYCLES_LINE;

    if ((line % I8039_SLICE) == I8039_SLICE - 1) {
      dac.run(I8039_HZ / 60 / (LINES / I8039_SLICE));
    }
  }
}

// ---- Audio ----

void junofrst::renderAudio(int16_t *out, int n) {
  Ay8910::renderBank(ay, soundregs, out, n);
  dac.render(out, n);
}

// ---- Video ----

void junofrst::prepare_frame(void) {
  vblank.read(video);
  for (int i = 0; i < 16; i++) {
    pens[i] = pen_rgb[video.palette[i]];
  }
}

// ROT90: panel row 2 + (255 - native X) / 8, column = native Y - 16
void junofrst::render_row(short row) {
  if (row < 2 || row > 33 || !videoram) {
    return;
  }

  for (int dy = 0; dy < 8; dy++) {
    const int x = 255 - ((row - 2) * 8 + dy);
    const uint8_t *column = videoram + 16 * 128 + (x >> 1);
    const int shift = (x & 1) ? 4 : 0;
    uint16_t *dst = frame_buffer + dy * 224;
    for (int sx = 0; sx < 224; sx++) {
      dst[sx] = pens[(column[sx << 7] >> shift) & 0x0f];
    }
  }
}

#ifdef LED_PIN
// colors of the logo: deep blue edge, light blue, cyan center
static const CRGB JUNOFRST_LED_DEEP(0x000094), JUNOFRST_LED_BLUE(0x005cd8),
    JUNOFRST_LED_CYAN(0x00aadd);

void junofrst::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      JUNOFRST_LED_DEEP, JUNOFRST_LED_BLUE, JUNOFRST_LED_CYAN, JUNOFRST_LED_CYAN,
      JUNOFRST_LED_CYAN, JUNOFRST_LED_BLUE, JUNOFRST_LED_DEEP};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// cyan while warp is held
void junofrst::gameLeds(CRGB *leds) {
  if (!(input->buttons_get() & WARP_KEY)) {
    menuLeds(leds);
    return;
  }
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = JUNOFRST_LED_CYAN;
  }
}
#endif

#endif
