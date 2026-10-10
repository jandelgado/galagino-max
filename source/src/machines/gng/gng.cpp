#include "gng.h"
#include "gng_rom.h"
#include "gng_bank.h"
#include "gng_sound.h"
#include "gng_chars.h"
#include "gng_tiles.h"
#include "gng_sprites.h"
#include "gng_sid.h"
#include "gng_dipswitches.h"
#include "../../emulation/arena.h"
#include "../../emulation/spriteflip.h"

// main 6809: E clock 1.5 MHz, IRQ at vblank. Run in quarters, the sound
// Z80 between them (its IRQ comes 4 x per frame).
static const int MAIN_CYCLES = 1500000 / 60;
static const uint16_t ROM_BASE = 0x4000;   // gng_rom = CPU $4000-$FFFF
static const uint16_t FIXED_BASE = 0x6000; // $6000-$FFFF never banked
static const uint16_t BANK_SIZE = 0x2000;  // $4000-$5FFF
static const uint8_t BANK_FIXED = 4;       // $3E00 = 4: gng_rom $4000

// sound Z80: 3 MHz, IRQ 4 x per frame (MAME 4 * 60 Hz). The IRQ handler
// returns to an "EI; JR $010B" idle loop (MAME trace 2026-10-10).
static const uint16_t SOUND_IRQS = 4;
static const uint16_t SOUND_STEPS = 12500 / 4; // 3 MHz / 240 Hz, >= 4 cycles each
static const uint16_t SOUND_IDLE_PC = 0x010c;

// LS259 at $3D00-$3D07, data bit 0 (MAME mainlatch)
static const uint16_t LATCH_SOUND_RUN = 0x3d01; // low: sound CPU + YM reset

// sound latch: the driver uses bits 0-5 (MAME sweep 2026-10-10)
static const uint8_t SND_MASK = 0x3f;
static const uint8_t SND_DEATH = 0x31; // death jingle, after 3f 00

// main CPU inputs (active low)
static const uint8_t SYS_START1 = 0x01, SYS_COIN1 = 0x40;
static const uint8_t P1_RIGHT = 0x01, P1_LEFT = 0x02, P1_DOWN = 0x04, P1_UP = 0x08;
static const uint8_t P1_FIRE = 0x10, P1_JUMP = 0x20;

gng::gng()
  : gng_rom(gng_rom_blob),
    gng_sprites0(gng_sprites0_blob),
    gng_sprites1(gng_sprites1_blob),
    gng_sprites2(gng_sprites2_blob),
    gng_chars(gng_chars_blob),
    gng_tile_map(gng_tile_map_blob),
    gng_tiles(gng_tiles_blob),
    gng_bank(gng_bank_blob),
    gng_sound(gng_sound_blob),
    gng_sid(gng_sid_blob) {
  // largest first: block A rom + sprites0, B sprites1 + 2 + chars
  rom = gng_rom.data();
  sprite_banks[0] = gng_sprites0.data();
  sprite_banks[1] = gng_sprites1.data();
  sprite_banks[2] = gng_sprites2.data();
  chars = gng_chars.data();
  tile_map = gng_tile_map.data();
  tiles = gng_tiles.data();
  bank_rom = gng_bank.data();
  sound_rom = gng_sound.data();

  // skip fully transparent chars (most of the text layer) per tile
  for (uint16_t c = 0; c < gng_chars.size(); c++) {
    bool blank = true;
    for (int r = 0; r < 8; r++) {
      blank = blank && chars[c][r] == 0xffff;
    }
    if (blank) {
      blank_chars[c >> 5] |= 1u << (c & 31);
    }
  }

  // the tune copies its player from $4000-$4CFF to $C000 and plays there
  tune_ok = psidParse(gng_sid.data(), gng_sid.size(), &tune);
  if (tune_ok) {
    tune_ok = player.load(tune, Arena::alloc<uint8_t>(TUNE_RAM_SIZE), TUNE_RAM_SIZE);
  }
  if (tune_ok) {
    player.alias(0xc000, 0x4000, 0x0d00);
  } else {
    printf("gng: Ghosts_n_Goblins.sid is not a supported PSID\n");
  }
}

void gng::reset() {
  machineBase::reset();
  bank = 0;
  memset(scroll, 0, sizeof(scroll));
  memset(sprite_buffer, 0, sizeof(sprite_buffer));
  sound_latch = 0;
  sound_reset = true;
  memset(ym_addr, 0, sizeof(ym_addr));
  music_subtune = 0;
  music_request = 0;

  m6809_reset(&main_cpu); // reset vector from $FFFE, read through m6809_read

  // instruction fetches from the fixed ROM skip the callback chain; must
  // follow m6809_reset(), which clears it. Banked code goes through
  // m6809_read_opcode.
  main_cpu.rom_direct = rom + FIXED_BASE - ROM_BASE;
  main_cpu.rom_base = FIXED_BASE;
  main_cpu.rom_size = 0x10000 - FIXED_BASE;
}

unsigned char gng::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

unsigned char gng::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= FIXED_BASE) {
    return rom[addr - ROM_BASE];
  }
  if (addr >= ROM_BASE) {
    if (bank == BANK_FIXED) {
      return rom[addr - ROM_BASE];
    }
    return bank_rom[BANK_SIZE * bank + addr - ROM_BASE];
  }
  if (addr < 0x3000) {
    return memory[MAIN_RAM + addr];
  }

  const unsigned int keys = input->buttons_get();
  switch (addr) {
    case 0x3000: { // SYSTEM
      uint8_t v = 0xff;
      if (keys & BUTTON_START) {
        v &= ~SYS_START1;
      }
      if (keys & BUTTON_COIN) { // level: the game debounces it
        v &= ~SYS_COIN1;
      }
      return v;
    }
    case 0x3001: { // P1; jump on START (one action button + EXTRA)
      uint8_t v = 0xff;
      if (keys & BUTTON_RIGHT) {
        v &= ~P1_RIGHT;
      }
      if (keys & BUTTON_LEFT) {
        v &= ~P1_LEFT;
      }
      if (keys & BUTTON_DOWN) {
        v &= ~P1_DOWN;
      }
      if (keys & BUTTON_UP) {
        v &= ~P1_UP;
      }
      if (keys & (BUTTON_FIRE | BUTTON_A | BUTTON_L1)) {
        v &= ~P1_FIRE;
      }
      if (keys & (BUTTON_START | BUTTON_B | BUTTON_R1)) {
        v &= ~P1_JUMP;
      }
      return v;
    }
    case 0x3003:
      return GNG_DIP_A;
    case 0x3004:
      return GNG_DIP_B;
  }
  return 0xff;
}

void gng::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr < 0x3000) {
    memory[MAIN_RAM + addr] = val;
    return;
  }
  if (addr >= 0x3800 && addr < 0x3a00) { // palette: ext (B) $38xx, RG $39xx
    memory[PALETTE_RAM + addr - 0x3800] = val;
    return;
  }
  if (addr == 0x3a00) {
    wrLatch(val);
    return;
  }
  if (addr >= 0x3b08 && addr <= 0x3b0b) {
    scroll[addr - 0x3b08] = val;
    return;
  }
  if (addr == 0x3c00) { // sprite DMA
    memcpy(sprite_buffer, memory + SPRITE_RAM, sizeof(sprite_buffer));
    return;
  }
  if (addr == LATCH_SOUND_RUN) { // other latch bits: flip, coin counters
    const bool reset = !(val & 1);
    if (reset && !sound_reset) {
      ResetZ80(&cpu[0]);
      memset(soundregs, 0, sizeof(soundregs)); // YM2203 reset
    }
    sound_reset = reset;
    return;
  }
  if (addr == 0x3e00) {
    bank = val == BANK_FIXED ? BANK_FIXED : (val & 3);
  }
}

void gng::wrLatch(uint8_t value) {
#ifdef LED_PIN
  if ((value & SND_MASK) == SND_DEATH) {
    led_flash = LED_FLASH_FRAMES;
  }
#endif
  sound_latch = value;
}

unsigned char gng::opZ80(unsigned short Addr) {
  return rdZ80(Addr);
}

unsigned char gng::rdZ80(unsigned short Addr) {
  if (Addr < gng_sound.size()) {
    return sound_rom[Addr];
  }
  if (Addr >= 0xc000 && Addr < 0xc800) {
    return memory[SOUND_RAM + Addr - 0xc000];
  }
  if (Addr == 0xc800) {
    return sound_latch;
  }
  return 0xff;
}

// $E000/$E001 YM2203 #1, $E002/$E003 #2: even = register select, odd =
// data. Only the SSG registers 0-15 are kept (AY layout, 16 per chip).
// The game sets the SSG prescaler to 1/2 (750 kHz AY clock, audio.cpp).
void gng::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0xc000 && Addr < 0xc800) {
    memory[SOUND_RAM + Addr - 0xc000] = Value;
    return;
  }
  if (Addr >= 0xe000 && Addr <= 0xe003) {
    const uint8_t chip = (Addr >> 1) & 1;
    if (!(Addr & 1)) {
      ym_addr[chip] = Value;
    } else if (ym_addr[chip] < 16) {
      soundregs[16 * chip + ym_addr[chip]] = Value;
    }
  }
}

// cycles counted per call: m6809_step() overwrites its return value
// per instruction, total_cycles adds them up
void gng::runMain(int cycles) {
  main_cpu.total_cycles = 0;
  while (main_cpu.total_cycles < cycles) {
    m6809_step(&main_cpu, 4);
  }
}

void gng::runSound() {
  if (sound_reset) {
    return;
  }
  Z80 *z = &cpu[0];
  IntZ80(z, INT_RST38);
  for (int i = 0; i < SOUND_STEPS; i++) {
    if (z->PC.W == SOUND_IDLE_PC && (z->IFF & IFF_1)) {
      break;
    }
    StepZ80(z);
  }
}

void gng::run_frame() {
  game_started = 1;

  for (int q = 0; q < SOUND_IRQS; q++) {
    runMain(MAIN_CYCLES / SOUND_IRQS);
    runSound();
  }

  m6809_irq(&main_cpu);
}

void gng::prepare_frame() {
  active_sprites = 0;
}

void gng::blit_tile(short row, char col) {
}

void gng::blit_sprite(short row, unsigned char s) {
}

void gng::render_row(short row) {
  memset(frame_buffer, 0, WIDTH * 8 * sizeof(*frame_buffer));
}

void gng::renderSid(int16_t *out, int n) {
  memset(out, 0, n * sizeof(*out));
}

#ifdef LED_PIN
// logo colors (quantized logos/gng.png)
static const CRGB DARK = CRGB(0x581805);
static const CRGB RUST = CRGB(0x862905);
static const CRGB BROWN = CRGB(0x8a4307);
static const CRGB ORANGE = CRGB(0xb96507);

void gng::menuLeds(CRGB *leds) {
  const CRGB pattern[NUM_LEDS] = {DARK, RUST, BROWN, ORANGE, BROWN, RUST, DARK};
  memcpy(leds, pattern, sizeof(pattern));
}

// logo colors, red while Arthur dies
void gng::gameLeds(CRGB *leds) {
  if (led_flash) {
    led_flash--;
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = LED_RED;
    }
    return;
  }
  menuLeds(leds);
}
#endif
