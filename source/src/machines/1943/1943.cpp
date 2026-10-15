#include "1943.h"
#include "1943_rom.h"
#include "1943_sound.h"
#include "1943_chars.h"
#include "1943_bg1.h"
#include "1943_bg2.h"
#include "1943_sprites.h"
#include "1943_tilemap.h"
#include "1943_cmap.h"
#include "1943_dipswitches.h"

// main Z80 6 MHz: twice commando's 3 MHz budget per quarter frame
static const uint16_t MAIN_STEPS = 2 * INST_PER_FRAME;
static const uint16_t ROM_END = 0x8000;
static const uint16_t BANK_END = 0xc000;
static const uint16_t BANK_SIZE = 0x4000;

// sound Z80: 3 MHz, IRQ 4 x per frame (MAME 4 * 60 Hz). The IRQ handler
// returns to an "EI; JR $-1" idle loop at $007B; stepping it is wasted.
static const uint16_t SOUND_ROM_END = 0x8000;
static const uint16_t SOUND_IRQS = 4;
static const uint16_t SOUND_STEPS = 12500 / 4; // 3 MHz / 240 Hz, >= 4 cycles each
static const uint16_t SOUND_IDLE_PC = 0x007c;
static const uint32_t FM_RATE = 24000;         // audio.cpp I2S rate
static const int FM_SHIFT = 9; // full-scale chip -> 64, an SSG channel peaks at 75

// $C804
static const uint8_t C804_BANK_SHIFT = 2;
static const uint8_t C804_BANK_MASK = 7;
static const uint8_t C804_SOUND_RESET = 0x20;

_1943::_1943()
  : _1943_tilemap(_1943_tilemap_blob),
    _1943_bg2(_1943_bg2_blob),
    _1943_chars(_1943_chars_blob),
    _1943_cmap_chars(_1943_cmap_chars_blob),
    _1943_cmap_bg1(_1943_cmap_bg1_blob),
    _1943_cmap_bg2(_1943_cmap_bg2_blob),
    _1943_cmap_sprites(_1943_cmap_sprites_blob),
    _1943_pen_masks(_1943_pen_masks_blob),
    _1943_rom(_1943_rom_blob),
    _1943_sound(_1943_sound_blob),
    _1943_bg1(_1943_bg1_blob),
    _1943_sprites(_1943_sprites_blob) {
  // this order only (first fit): A tilemap, B bg2 and chars, A color maps
  tilemap = _1943_tilemap.data();
  bg2_tiles = _1943_bg2.data();
  chars = _1943_chars.data();
  cmap_chars = _1943_cmap_chars.data();
  cmap_bg1 = _1943_cmap_bg1.data();
  cmap_bg2 = _1943_cmap_bg2.data();
  cmap_sprites = _1943_cmap_sprites.data();
  bg1_clear = _1943_pen_masks.data()[0];
  spr_behind = _1943_pen_masks.data()[1];
  rom = _1943_rom.data();
  sound_rom = _1943_sound.data();
  bg1_tiles = _1943_bg1.data();
  sprite_gfx = _1943_sprites.data();

  // skip fully transparent chars (most of the text layer)
  for (uint16_t c = 0; c < _1943_chars.size(); c++) {
    bool blank = true;
    for (int r = 0; r < 8; r++) {
      blank = blank && chars[c][r] == 0;
    }
    if (blank) {
      blank_chars[c >> 5] |= 1u << (c & 31);
    }
  }
}

void _1943::reset() {
  machineBase::reset();
  c804 = 0;
  d806 = 0;
  memset(scroll, 0, sizeof(scroll));
  sound_latch = 0;
  irq_pending = false;
  sound_reset = false;
  memset(ym_addr, 0, sizeof(ym_addr));
  fm[0].reset();
  fm[1].reset();
}

unsigned char _1943::opZ80(unsigned short Addr) {
  return rdZ80(Addr);
}

unsigned char _1943::rdZ80(unsigned short Addr) {
  if (current_cpu) {
    return rdSound(Addr);
  }
  if (Addr < ROM_END) {
    return rom[Addr];
  }
  if (Addr < BANK_END) {
    const uint8_t bank = (c804 >> C804_BANK_SHIFT) & C804_BANK_MASK;
    return rom[ROM_END + bank * BANK_SIZE + Addr - ROM_END];
  }
  if (Addr >= 0xe000) {
    return memory[RAM + Addr - 0xe000];
  }
  if (Addr >= 0xd000 && Addr < 0xd800) {
    return memory[FG_CODE + Addr - 0xd000];
  }
  if (Addr >= 0xd800 && Addr <= 0xd804) {
    return scroll[Addr - 0xd800];
  }
  return rdInput(Addr);
}

unsigned char _1943::rdInput(unsigned short Addr) {
  const unsigned int keys = input->buttons_get();
  switch (Addr) {
    case 0xc000: { // SYSTEM: b6 COIN1, b0 START1, b3 VBLANK (unused, high)
      unsigned char v = 0xff;
      if (keys & BUTTON_COIN) { // level: the game debounces it
        v &= ~0x40;
      }
      if (keys & BUTTON_START) {
        v &= ~0x01;
      }
      return v;
    }
    case 0xc001: { // P1: b5 button 2, b4 fire, b3 U, b2 D, b1 L, b0 R
      unsigned char v = 0xff;
      if (keys & BUTTON_RIGHT) {
        v &= ~0x01;
      }
      if (keys & BUTTON_LEFT) {
        v &= ~0x02;
      }
      if (keys & BUTTON_DOWN) {
        v &= ~0x04;
      }
      if (keys & BUTTON_UP) {
        v &= ~0x08;
      }
      if (keys & (BUTTON_FIRE | BUTTON_L1)) {
        v &= ~0x10;
      }
      if (keys & (BUTTON_START | BUTTON_R1)) {
        v &= ~0x20;
      }
      return v;
    }
    case 0xc003:
      return _1943_DIP_A;
    case 0xc004:
      return _1943_DIP_B;
    case 0xc007: // bootleg: protection patched out, expects 0
      return 0x00;
  }
  return 0xff;
}

void _1943::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu) {
    wrSound(Addr, Value);
    return;
  }
  if (Addr >= 0xe000) {
    memory[RAM + Addr - 0xe000] = Value;
    return;
  }
  if (Addr >= 0xd000 && Addr < 0xd800) {
    memory[FG_CODE + Addr - 0xd000] = Value;
    return;
  }
  if (Addr >= 0xd800 && Addr <= 0xd804) {
    scroll[Addr - 0xd800] = Value;
    return;
  }

  switch (Addr) {
    case 0xc800:
      sound_latch = Value;
      game_started = 1; // first command comes right after the boot test
      return;
    case 0xc804: { // also coin counters, flip (upright: unused)
      const bool hold = Value & C804_SOUND_RESET;
      if (hold && !sound_reset) {
        ResetZ80(&cpu[1]);
      }
      sound_reset = hold;
      c804 = Value;
      return;
    }
    case 0xd806:
      d806 = Value;
      return;
  }
}

unsigned char _1943::rdSound(unsigned short Addr) {
  if (Addr < SOUND_ROM_END) {
    return sound_rom[Addr];
  }
  if (Addr >= 0xc000 && Addr < 0xc800) {
    return memory[SOUND_RAM + Addr - 0xc000];
  }
  if (Addr == 0xc800) {
    return sound_latch;
  }
  return 0x00; // $D800 MCU latch: no MCU on the bootleg
}

// $E000/$E001 YM2203 #1, $E002/$E003 #2: even = address, odd = data.
// SSG registers 0-15 go to soundregs (AY layout, 16 per chip), the rest
// to the FM queue. The game sets prescaler 1/3: SSG 750 kHz (audio.cpp).
void _1943::wrSound(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0xc000 && Addr < 0xc800) {
    memory[SOUND_RAM + Addr - 0xc000] = Value;
    return;
  }
  if (Addr < 0xe000 || Addr > 0xe003) {
    return;
  }

  const uint8_t chip = (Addr >> 1) & 1;
  if (!(Addr & 1)) {
    ym_addr[chip] = Value;
    fm[chip].writeAddress(Value);
    return;
  }
  if (ym_addr[chip] < 16) {
    soundregs[16 * chip + ym_addr[chip]] = Value;
    return;
  }
  fm[chip].writeData(ym_addr[chip], Value);
}

void _1943::runSound() {
  if (sound_reset) {
    return;
  }
  current_cpu = 1;
  Z80 *z = &cpu[1];
  IntZ80(z, INT_RST38);
  for (int i = 0; i < SOUND_STEPS; i++) {
    if (z->PC.W == SOUND_IDLE_PC && (z->IFF & IFF_1)) {
      break;
    }
    StepZ80(z);
  }
  current_cpu = 0;
}

void _1943::run_frame() {
  for (int q = 0; q < SOUND_IRQS; q++) {
    current_cpu = 0;
    for (int i = 0; i < MAIN_STEPS; i++) {
      // irq0_line_hold: the main loop runs with DI and only briefly EI
      if (irq_pending && (cpu[0].IFF & IFF_1)) {
        IntZ80(&cpu[0], INT_RST38);
        irq_pending = false;
      }
      StepZ80(&cpu[0]);
    }
    runSound();
  }

  vblank.publish([this](VideoState &v) {
    for (uint8_t s = 0; s < SPRITES; s++) {
      memcpy(v.sprites[s], memory + SPRITE_RAM + 32 * s, 4);
    }
    v.bg1_x = scroll[0] | (scroll[1] << 8);
    v.bg1_y = scroll[2];
    v.bg2_x = scroll[3] | (scroll[4] << 8);
    v.c804 = c804;
    v.d806 = d806;
  });
  irq_pending = true;
}

void _1943::renderFm(int16_t *out, int n) {
  static const int CHUNK = 64; // Audio renders 64 samples per call
  for (int done = 0; done < n; done += CHUNK) {
    const int len = n - done < CHUNK ? n - done : CHUNK;
    int32_t acc[CHUNK] = {};
    fm[0].render(acc, len, FM_RATE);
    fm[1].render(acc, len, FM_RATE);
    for (int i = 0; i < len; i++) {
      out[done + i] = (int16_t)(acc[i] >> FM_SHIFT);
    }
  }
}

void _1943::prepare_frame() {
  vblank.read(video);
}

void _1943::render_row(short row) {
}
