#ifdef ENABLE_CBDASH

#include "cbdash.h"
#include "cbdash_logo.h"
#include "cbdash_rom_main.h"
#include "cbdash_rom_sound.h"
#include "cbdash_rom_bios.h"
#include "cbdash_rom_audio.h"

// HLE boot: CPU state at the BIOS jump into the game, measured in MAME
static constexpr uint16_t MAIN_IMAGE_ADDR = 0x0500;
static constexpr uint16_t SOUND_IMAGE_ADDR = 0x0200;
static constexpr uint16_t MAIN_ENTRY = 0x0503;
static constexpr uint8_t MAIN_SP = 0xff;
static constexpr uint16_t SOUND_ENTRY = 0x0203;   // JMP $0223, sets its own SP
static constexpr uint16_t CHARRAM_ADDR = 0x6000;
static constexpr uint8_t BIOS_GAME_LOADED = 0xf8; // ram[0]: BIOS NMI (coin) handler enters the game

static constexpr uint8_t DONGLE_ANSWER = 0x55;    // type 5: fixed value once latched
static constexpr uint8_t SOUND_CMD_MAIN_R = 0xc0; // e414 read, as MAME
static constexpr uint8_t SOUND_ACK_CMD = 0x80;    // command pending for the sound CPU
static constexpr uint8_t SOUND_ACK_DATA = 0x40;   // data pending for the main CPU
static constexpr uint8_t AY_REGS = 14;
static constexpr uint8_t LED_FLASH_FRAMES = 8;

// 750 kHz / 60 Hz = 12500 cycles; VBLANK is 32 of 272 scanlines
static constexpr int MAIN_CYCLES_ACTIVE = 11030;
static constexpr int MAIN_CYCLES_VBLANK = 1470;
// 500 kHz, sound NMI on every 16th scanline: 17 per frame
static constexpr int AUDIO_NMI_SLICES = 17;
static constexpr int AUDIO_CYCLES_PER_SLICE = 490;

// DECO 222: bits 5 and 6 of every opcode fetch are swapped
static inline uint8_t deco222(uint8_t v) {
  return (v & 0x9f) | ((v & 0x20) << 1) | ((v & 0x40) >> 1);
}

// c800-cfff mirror fg/color RAM with row and column swapped
static inline uint16_t mirror(uint16_t offs) {
  return ((offs >> 5) & 0x1f) | ((offs & 0x1f) << 5);
}

cbdash::cbdash()
  : cbdash_rom_main(cbdash_rom_main_blob),
    cbdash_rom_sound(cbdash_rom_sound_blob),
    cbdash_rom_bios(cbdash_rom_bios_blob),
    cbdash_rom_audio(cbdash_rom_audio_blob) {
  memset(&cpu_main, 0, sizeof(cpu_main));
  memset(&cpu_audio, 0, sizeof(cpu_audio));
  rom_bios = cbdash_rom_bios.data();
  rom_audio = cbdash_rom_audio.data();
}

void cbdash::start() {
  // start() runs twice; Arena has no free
  if (!main_ram) {
    main_ram = Arena::alloc(MAIN_RAM_SIZE);
  }
  boot();
}

void cbdash::reset() {
  machineBase::reset();

  // the first reset() runs before start() allocated the RAM
  if (main_ram) {
    boot();
  }
}

// Replaces the tape load with the RAM and CPU state the BIOS leaves behind
void cbdash::boot() {
  memset(main_ram, 0, MAIN_RAM_SIZE);
  memset(memory, 0, MEM_END);
  memcpy(main_ram + MAIN_IMAGE_ADDR, cbdash_rom_main.data(), cbdash_rom_main.size());
  memcpy(memory + SOUND_RAM_OFFSET + SOUND_IMAGE_ADDR, cbdash_rom_sound.data(), cbdash_rom_sound.size());
  main_ram[0x0000] = BIOS_GAME_LOADED;

  memset(palette_ram, 0, sizeof(palette_ram));
  ccb = sound_latch = sound_latch2 = sound_ack = 0;
  ay_addr[0] = ay_addr[1] = 0;
  audio_nmi_enable = audio_reset = dongle_latch = coin_prev = false;
  led_flash = 0;

  cpu_main.read = main_read;
  cpu_main.write = main_write;
  cpu_main.fetch = main_fetch;
  cpu_main.user = this;
  m6502_reset(&cpu_main);
  cpu_main.pc = MAIN_ENTRY;
  cpu_main.sp = MAIN_SP;

  cpu_audio.read = audio_read;
  cpu_audio.write = audio_write;
  cpu_audio.fetch = 0;
  cpu_audio.user = this;
  m6502_reset(&cpu_audio);
  cpu_audio.pc = SOUND_ENTRY;
}

// IN0: b0 right, b1 left, b2 up, b3 down, b4 button 1 (active high)
uint8_t cbdash::in0() {
  unsigned int keys = input->buttons_get();
  uint8_t v = 0;
  if (keys & BUTTON_RIGHT) { v |= 0x01; }
  if (keys & BUTTON_LEFT)  { v |= 0x02; }
  if (keys & BUTTON_UP)    { v |= 0x04; }
  if (keys & BUTTON_DOWN)  { v |= 0x08; }
  if (keys & BUTTON_FIRE)  { v |= 0x10; }
  return v;
}

// IN2: b3 start 1 (active high), b7 coin 1 (active low)
uint8_t cbdash::in2() {
  unsigned int keys = input->buttons_get();
  uint8_t v = 0xc0;
  if (keys & BUTTON_START) { v |= 0x08; }
  if (keys & BUTTON_COIN)  { v &= ~0x80; }
  return v;
}

// ---------------------------------------------------------------------------
// Main CPU
// ---------------------------------------------------------------------------
uint8_t cbdash::main_fetch(m6502_t *cpu, uint16_t addr) {
  return deco222(main_read(cpu, addr));
}

uint8_t cbdash::main_read(m6502_t *cpu, uint16_t addr) {
  cbdash *s = (cbdash *)cpu->user;

  if (addr < 0xc000) { return s->main_ram[addr]; }
  if (addr >= 0xf000) { return s->rom_bios[addr & 0x0fff]; }
  if (addr < 0xc400) { return s->memory[FG_RAM_OFFSET + (addr & 0x3ff)]; }
  if (addr < 0xc800) { return s->memory[COLOR_RAM_OFFSET + (addr & 0x3ff)]; }
  if (addr < 0xcc00) { return s->memory[FG_RAM_OFFSET + mirror(addr & 0x3ff)]; }
  if (addr < 0xd000) { return s->memory[COLOR_RAM_OFFSET + mirror(addr & 0x3ff)]; }
  if (addr < 0xdc00) { return s->memory[TILE_RAM_OFFSET + (addr - 0xd000)]; }
  if (addr >= 0xe000 && addr < 0xe100) { return s->palette_ram[addr & 0x1f]; }

  switch (addr) {
    case 0xe300: return CBDASH_DSW1 | (s->vblank ? 0x80 : 0x00);
    case 0xe301: return CBDASH_DSW2;
    case 0xe414: return SOUND_CMD_MAIN_R;
    case 0xe500: return s->dongle_latch ? DONGLE_ANSWER : 0xff;
    case 0xe600: return s->in0();
    case 0xe601: return 0x00; // cocktail player
    case 0xe602: return s->in2();
    case 0xe700: return s->sound_latch2;
    case 0xe701: return s->sound_ack;
  }
  return (addr >= 0xe603 && addr <= 0xe606) ? 0x00 : 0xff; // quadrature decoders
}

void cbdash::main_write(m6502_t *cpu, uint16_t addr, uint8_t val) {
  cbdash *s = (cbdash *)cpu->user;

  if (addr < 0xc000) { s->main_ram[addr] = val; return; }
  if (addr < 0xc400) { s->memory[FG_RAM_OFFSET + (addr & 0x3ff)] = val; return; }
  if (addr < 0xc800) { s->memory[COLOR_RAM_OFFSET + (addr & 0x3ff)] = val; return; }
  if (addr < 0xcc00) { s->memory[FG_RAM_OFFSET + mirror(addr & 0x3ff)] = val; return; }
  if (addr < 0xd000) { s->memory[COLOR_RAM_OFFSET + mirror(addr & 0x3ff)] = val; return; }
  if (addr < 0xdc00) { s->memory[TILE_RAM_OFFSET + (addr - 0xd000)] = val; return; }
  if (addr >= 0xe000 && addr < 0xe100) { s->palette_ram[addr & 0x1f] = val; return; }

  switch (addr) {
    case 0xe400: // b0: sound CPU reset, active high
      s->audio_reset = (val & 0x01) != 0;
      if (s->audio_reset) {
        s->audio_nmi_enable = false;
        m6502_reset(&s->cpu_audio);
      }
      return;

    case 0xe410:
      s->ccb = val;
      return;

    case 0xe414: // sound command: latch + IRQ to the sound CPU
      s->sound_latch = val;
      s->sound_ack = (s->sound_ack | SOUND_ACK_CMD) & ~SOUND_ACK_DATA;
      s->cpu_audio.irq = 1;
      s->led_flash = LED_FLASH_FRAMES;
      return;

    case 0xe417: // main NMI clear
      s->cpu_main.nmi = 0;
      return;

    case 0xe501: // dongle type 5: 0xCx latches the fixed answer
      if ((val & 0xf0) == 0xc0) {
        s->dongle_latch = true;
      }
      return;
  }
  // watchdog, video mode/scroll/center registers, coin counter: unused by cbdash
}

// ---------------------------------------------------------------------------
// Sound CPU (6502, plain) + 2x AY-3-8910
// ---------------------------------------------------------------------------
uint8_t cbdash::audio_read(m6502_t *cpu, uint16_t addr) {
  cbdash *s = (cbdash *)cpu->user;

  if (addr < 0x1000) { return s->memory[SOUND_RAM_OFFSET + addr]; }
  if (addr >= 0xf800) { return s->rom_audio[addr & 0x07ff]; }

  if (addr < 0x1800) { // reading enables the NMI too
    s->audio_nmi_enable = true;
    return 0xff;
  }
  if (addr < 0x2000) {
    s->sound_ack &= ~SOUND_ACK_DATA;
    return 0xff;
  }
  if (addr >= 0xa000 && addr < 0xb000) {
    cpu->irq = 0;
    s->sound_ack &= ~SOUND_ACK_CMD;
    return s->sound_latch;
  }
  return 0xff;
}

void cbdash::audio_write(m6502_t *cpu, uint16_t addr, uint8_t val) {
  cbdash *s = (cbdash *)cpu->user;

  if (addr < 0x1000) { s->memory[SOUND_RAM_OFFSET + addr] = val; return; }
  if (addr < 0x1800) { s->audio_nmi_enable = true; return; }
  if (addr < 0x2000) { s->sound_ack &= ~SOUND_ACK_DATA; return; }

  if (addr >= 0x2000 && addr < 0x3000) {
    if (s->ay_addr[0] < AY_REGS) { s->soundregs[0x00 + s->ay_addr[0]] = val; }
    return;
  }
  if (addr >= 0x4000 && addr < 0x5000) { s->ay_addr[0] = val & 0x0f; return; }
  if (addr >= 0x6000 && addr < 0x7000) {
    if (s->ay_addr[1] < AY_REGS) { s->soundregs[0x10 + s->ay_addr[1]] = val; }
    return;
  }
  if (addr >= 0x8000 && addr < 0x9000) { s->ay_addr[1] = val & 0x0f; return; }

  if (addr >= 0xc000 && addr < 0xd000) {
    s->sound_latch2 = val;
    s->sound_ack |= SOUND_ACK_DATA;
  }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void cbdash::run_frame(void) {
  // coin raises NMI, the game clears it via e417
  bool coin_now = (input->buttons_get() & BUTTON_COIN) != 0;
  if (coin_now && !coin_prev) {
    cpu_main.nmi = 1;
  }
  coin_prev = coin_now;

  // the game busy-waits on DSW1 b7, so it must see VBLANK once per frame
  vblank = false;
  m6502_exec(&cpu_main, MAIN_CYCLES_ACTIVE);

  vblank_state.publish([this](cbdash_video::VideoState &v) {
    memcpy(v.fg, memory + FG_RAM_OFFSET, sizeof(v.fg));
    memcpy(v.color, memory + COLOR_RAM_OFFSET, sizeof(v.color));
    memcpy(v.palette, palette_ram, sizeof(v.palette));
    v.ccb = ccb;
  });
  vblank = true;
  m6502_exec(&cpu_main, MAIN_CYCLES_VBLANK);

  for (int i = 0; i < AUDIO_NMI_SLICES && !audio_reset; i++) {
    m6502_exec(&cpu_audio, AUDIO_CYCLES_PER_SLICE);
    if (audio_nmi_enable) {
      cpu_audio.nmi = 1;
    }
  }

  if (led_flash) {
    led_flash--;
  }
  if (!game_started) {
    game_started = 1;
  }
}

void cbdash::prepare_frame(void) {
  using namespace cbdash_video;
  vblank_state.read(video);

  // pen p shows palette RAM entry p ^ 16 (inverted A4)
  for (int p = 0; p < PENS; p++) {
    pens[p] = color565(video.palette[p ^ 16]);
  }
  sprite_count = collectSprites(video, sprites);
}

void cbdash::render_row(short row) {
  cbdash_video::renderStrip(frame_buffer, row, main_ram + CHARRAM_ADDR, video, pens, sprites, sprite_count);
}

const Logo &cbdash::logo(void) {
  return cbdash_logo;
}

#ifdef LED_PIN
static const CRGB CBDASH_LED_RED = CRGB(0xe60000);
static const CRGB CBDASH_LED_GOLD = CRGB(0xffc828);
static const CRGB CBDASH_LED_DARK = CRGB(0x6e0000);

void cbdash::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      CBDASH_LED_DARK, CBDASH_LED_RED, CBDASH_LED_GOLD, CBDASH_LED_GOLD,
      CBDASH_LED_GOLD, CBDASH_LED_RED, CBDASH_LED_DARK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// gold flash for a few frames after each sound command (diamonds, rocks)
void cbdash::gameLeds(CRGB *leds) {
  if (!led_flash) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, CBDASH_LED_GOLD);
}
#endif

#endif // ENABLE_CBDASH
