#include "../../catalog.h"
#ifdef GAME_ELEVATOR

#include "elevator.h"
#include "elevator_main_rom.h"
#include "elevator_sound_rom.h"
#include "elevator_gfx_rom.h"
#include "elevator_draw_order.h"

// main 4 MHz : sound 3 MHz, interleaved 4 : 3 instructions (as 1942)
static constexpr uint16_t SLICES = 4 * INST_PER_FRAME / 3;
static constexpr uint8_t MAIN_STEPS = 4, SOUND_STEPS = 3;
// sound IRQ 12 MHz / (2*4*16*16*10*16) = 36.62 Hz: every 1.638 frames
static constexpr uint16_t SOUND_IRQ_SLICES = SLICES * 60 * 10000 / 366211;

static constexpr uint16_t BANK_ADDR = 0x6000;
static constexpr uint16_t BANK1_ROM = 0x7000;   // bank 1: 6000-6fff empty, 7000-7fff ic52
static constexpr uint16_t BANK1_OFS = 0x8000;   // ic52 in elevator_main_rom
static constexpr uint16_t MAIN_ROM_END = 0x8000;
static constexpr uint16_t GFX_ROM_SIZE = 0x8000;
static constexpr uint16_t SOUND_ROM_END = 0x2000;
static constexpr uint8_t BANK_SEL = 0x80;
static constexpr uint8_t MODE_SPR_PAGE = 0x04;   // d600: sprite RAM d180 page

static constexpr uint8_t AY_PORT_A = 14, AY_PORT_B = 15;
static constexpr uint8_t AY_SOUND_STATUS = 2;   // port A -> main CPU IN4 b4-7
static constexpr uint8_t AY_NMI_MASK = 3;       // port B b0: 1 masks the latch NMI

// IN0 active low
static constexpr uint8_t IN0_LEFT = 0x01, IN0_RIGHT = 0x02, IN0_DOWN = 0x04, IN0_UP = 0x08;
static constexpr uint8_t IN0_SHOOT = 0x10, IN0_JUMP = 0x20;
// IN2 active low
static constexpr uint8_t IN2_COIN1 = 0x20, IN2_START1 = 0x40;
// IN3: coin 3 active high, rest active low
static constexpr uint8_t IN3_IDLE = 0xef;
static constexpr uint8_t IN4_IDLE = 0x0f;
// 8800/8801: no MCU on the bootleg (MAME fake_data_r / fake_status_r)
static constexpr uint8_t MCU_DATA = 0x00, MCU_STATUS = 0xff;
static constexpr uint8_t SOUND_FLAGS = 0x03, FLAG_LATCH = 0x08, FLAG_SEMA2 = 0x04;

// 4800/1 AY#1, 4802/3 AY#2, 4804-7 AY#3 (mirrored every 8 bytes)
static inline uint8_t soundChip(unsigned short Addr) {
  const uint8_t pair = (Addr & 7) >> 1;
  return pair < 2 ? 1 + pair : 3;
}

elevator::elevator()
  : elevator_main_rom(elevator_main_rom_blob), elevator_sound_rom(elevator_sound_rom_blob),
    elevator_gfx_rom(elevator_gfx_rom_blob), elevator_draw_order(elevator_draw_order_blob),
    charram(Arena::alloc<uint8_t>(CHARRAM_SIZE)) {
  main_rom = elevator_main_rom.data();
  sound_rom = elevator_sound_rom.data();
  gfx_rom = elevator_gfx_rom.data();
  draw_order = elevator_draw_order.data();
}

void elevator::reset() {
  machineBase::reset();
  memset(charram, 0, CHARRAM_SIZE);
  memset(scroll, 0, sizeof(scroll));
  memset(colorbank, 0, sizeof(colorbank));
  memset(ay_addr, 0, sizeof(ay_addr));
  video_mode = priority = bank_ctrl = 0;
  gfx_ptr = 0;
  soundlatch = 0;
  latch_full = sema2 = nmi_line = nmi_pending = false;
  main_irq = sound_irq = false;
  sound_irq_slices = 0;
  updateNmi();
}

void elevator::start() {
  game_started = 1;
}

// ---- CPUs ----

unsigned char elevator::opZ80(unsigned short Addr) {
  return current_cpu == MAIN_CPU ? rdMain(Addr) : rdSound(Addr);
}

unsigned char elevator::rdZ80(unsigned short Addr) {
  return current_cpu == MAIN_CPU ? rdMain(Addr) : rdSound(Addr);
}

void elevator::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == MAIN_CPU) {
    wrMain(Addr, Value);
    return;
  }
  wrSound(Addr, Value);
}

unsigned char elevator::rdMain(unsigned short Addr) {
  if (Addr < BANK_ADDR) {
    return main_rom[Addr];
  }
  if (Addr < MAIN_ROM_END) {
    if (!(bank_ctrl & BANK_SEL)) {
      return main_rom[Addr];
    }
    return Addr < BANK1_ROM ? 0 : main_rom[BANK1_OFS + Addr - BANK1_ROM];
  }
  if (Addr < 0x8800) {
    return memory[RAM_OFS + (Addr & 0x7ff)];
  }
  if (Addr < 0x9000) {
    return (Addr & 1) ? MCU_STATUS : MCU_DATA;
  }
  if (Addr < 0xc000) {
    return 0;   // character RAM is write only
  }
  if (Addr < 0xd000) {
    return memory[VRAM_OFS + (Addr & 0xfff)];
  }
  if (Addr < 0xd100) {
    return memory[COLSCROLL_OFS + (Addr & 0xff)];
  }
  if (Addr < 0xd200) {
    return memory[SPR_OFS + (Addr & 0xff)];
  }
  if (Addr < 0xd300) {
    return memory[PAL_OFS + (Addr & 0x7f)];
  }
  if ((Addr & 0xff00) == 0xd400) {
    return rdIo(Addr & 0x0f);
  }
  return 0;   // write-only registers, e000-ffff empty
}

// d400-d4ff, mirrored every 16 bytes
unsigned char elevator::rdIo(uint8_t reg) {
  switch (reg) {
    case 0x0: case 0x1: case 0x2: case 0x3:
      return 0;   // collision registers: never read by this game
    case 0x4: case 0x5: case 0x6: case 0x7: {
      const uint8_t v = gfx_ptr < GFX_ROM_SIZE ? gfx_rom[gfx_ptr] : 0;
      gfx_ptr++;
      return v;
    }
    case 0x8: return in0();
    case 0x9: return 0xff;   // IN1: player 2
    case 0xa: return ELEVATOR_DSW1;
    case 0xb: return in2();
    case 0xc: return IN3_IDLE;
    case 0xd: return IN4_IDLE | (soundregs[16 * AY_SOUND_STATUS + AY_PORT_A] & 0xf0);
    case 0xf:
      if (ay_addr[0] == AY_PORT_A) {
        return ELEVATOR_DSW2;
      }
      if (ay_addr[0] == AY_PORT_B) {
        return ELEVATOR_DSW3;
      }
      return soundregs[ay_addr[0]];
  }
  return 0;
}

// FIRE shoots; with UP held, FIRE jumps instead (one GPIO action button)
uint8_t elevator::in0(void) {
  const unsigned int keys = input->buttons_get();
  const bool fire = keys & (BUTTON_FIRE | BUTTON_A);
  const bool up = keys & BUTTON_UP;
  uint8_t v = 0xff;

  if (keys & BUTTON_LEFT) { v &= ~IN0_LEFT; }
  if (keys & BUTTON_RIGHT) { v &= ~IN0_RIGHT; }
  if (keys & BUTTON_DOWN) { v &= ~IN0_DOWN; }
  if (up) { v &= ~IN0_UP; }
  if (fire && !up) { v &= ~IN0_SHOOT; }
  if ((fire && up) || (keys & BUTTON_B)) { v &= ~IN0_JUMP; }
  return v;
}

uint8_t elevator::in2(void) {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xff;
  if (keys & BUTTON_COIN) { v &= ~IN2_COIN1; }
  if (keys & BUTTON_START) { v &= ~IN2_START1; }
  return v;
}

void elevator::wrMain(unsigned short Addr, unsigned char Value) {
  if (Addr < 0x8000) {
    return;
  }
  if (Addr < 0x8800) {
    memory[RAM_OFS + (Addr & 0x7ff)] = Value;
    return;
  }
  if (Addr < 0x9000) {
    return;   // MCU data / IRQ: no MCU
  }
  if (Addr < 0xc000) {
    charram[Addr - 0x9000] = Value;
    return;
  }
  if (Addr < 0xd000) {
    memory[VRAM_OFS + (Addr & 0xfff)] = Value;
    return;
  }
  if (Addr < 0xd100) {
    memory[COLSCROLL_OFS + (Addr & 0xff)] = Value;
    return;
  }
  if (Addr < 0xd200) {
    memory[SPR_OFS + (Addr & 0xff)] = Value;
    return;
  }
  switch (Addr & 0xff00) {
    case 0xd200: memory[PAL_OFS + (Addr & 0x7f)] = Value; return;
    case 0xd300: priority = Value; return;
    case 0xd400:
      if ((Addr & 0x0f) == 0x0e) { ay_addr[0] = Value & 0x0f; }
      if ((Addr & 0x0f) == 0x0f) { soundregs[ay_addr[0]] = Value; }
      return;
    case 0xd500: wrIo(Addr & 0x0f, Value); return;
    case 0xd600: video_mode = Value; return;
  }
}

// d500-d5ff, mirrored every 16 bytes
void elevator::wrIo(uint8_t reg, unsigned char Value) {
  switch (reg) {
    case 0x0: case 0x1: case 0x2: case 0x3: case 0x4: case 0x5:
      scroll[reg] = Value;
      return;
    case 0x6: case 0x7: colorbank[reg - 6] = Value; return;
    case 0x9: gfx_ptr = (gfx_ptr & 0xff00) | Value; return;
    case 0xa: gfx_ptr = (gfx_ptr & 0x00ff) | (Value << 8); return;
    case 0xb:
      soundlatch = Value;
      latch_full = true;
      updateNmi();
      return;
    case 0xc:
      sema2 = Value & 1;
      updateNmi();
      return;
    case 0xe: bank_ctrl = Value; return;
  }
  // 8 collision clear, d watchdog, f PAL: ignored
}

unsigned char elevator::rdSound(unsigned short Addr) {
  if (Addr < SOUND_ROM_END) {
    return sound_rom[Addr];
  }
  if ((Addr & 0xfc00) == 0x4000) {
    return memory[SND_RAM_OFS + (Addr & 0x3ff)];
  }
  if ((Addr & 0xf800) == 0x4800) {
    const uint8_t chip = soundChip(Addr);
    return soundregs[16 * chip + ay_addr[chip]];
  }
  if ((Addr & 0xf800) == 0x5000) {
    if (Addr & 1) {
      return SOUND_FLAGS | (latch_full ? FLAG_LATCH : 0) | (sema2 ? FLAG_SEMA2 : 0);
    }
    latch_full = false;
    updateNmi();
    return soundlatch;
  }
  return 0;
}

void elevator::wrSound(unsigned short Addr, unsigned char Value) {
  if ((Addr & 0xfc00) == 0x4000) {
    memory[SND_RAM_OFS + (Addr & 0x3ff)] = Value;
    return;
  }

  // even address: register select
  if ((Addr & 0xf800) == 0x4800) {
    const uint8_t chip = soundChip(Addr);
    if (!(Addr & 1)) {
      ay_addr[chip] = Value & 0x0f;
      return;
    }
    soundregs[16 * chip + ay_addr[chip]] = Value;
    if (chip == AY_NMI_MASK && ay_addr[chip] == AY_PORT_B) {
      updateNmi();
    }
    return;
  }

  if ((Addr & 0xf800) == 0x5000) {
    if (Addr & 1) {
      sema2 = false;
      updateNmi();
      return;
    }
    soundlatch &= 0x7f;
  }
}

// NMI = (latch full AND AY#3 port B b0 low) OR semaphore 2, rising edge
void elevator::updateNmi(void) {
  const bool unmasked = !(soundregs[16 * AY_NMI_MASK + AY_PORT_B] & 1);
  const bool line = (latch_full && unmasked) || sema2;
  if (line && !nmi_line) {
    nmi_pending = true;
  }
  nmi_line = line;
}

// ---- Frame ----

// one instruction, pending interrupts first (HOLD_LINE: kept until taken)
void elevator::stepCpu(uint8_t c) {
  current_cpu = c;
  Z80 &z = cpu[c];
  if (c == SOUND_CPU && nmi_pending) {
    nmi_pending = false;
    IntZ80(&z, INT_NMI);
  }
  bool &irq = c == MAIN_CPU ? main_irq : sound_irq;
  if (irq && (z.IFF & IFF_1)) {
    irq = false;
    IntZ80(&z, INT_RST38);
  }
  StepZ80(&z);
}

void elevator::run_frame(void) {
  for (uint16_t s = 0; s < SLICES; s++) {
    for (uint8_t i = 0; i < MAIN_STEPS; i++) {
      stepCpu(MAIN_CPU);
    }
    for (uint8_t i = 0; i < SOUND_STEPS; i++) {
      stepCpu(SOUND_CPU);
    }
    if (++sound_irq_slices >= SOUND_IRQ_SLICES) {
      sound_irq_slices = 0;
      sound_irq = true;
    }
  }
  publishVideo();
  main_irq = true;
}

void elevator::publishVideo(void) {
  vblank.publish([this](VideoState &v) {
    memcpy(v.tilemap, &memory[TILEMAP_OFS], sizeof(v.tilemap));
    memcpy(v.colscroll, &memory[COLSCROLL_OFS], sizeof(v.colscroll));
    memcpy(v.spr, &memory[SPR_OFS + ((video_mode & MODE_SPR_PAGE) ? SPR_SIZE : 0)], sizeof(v.spr));
    memcpy(v.pal, &memory[PAL_OFS], sizeof(v.pal));
    memcpy(v.scroll, scroll, sizeof(v.scroll));
    memcpy(v.colorbank, colorbank, sizeof(v.colorbank));
    v.mode = video_mode;
    v.prio = priority;
  });
}

// ---- Video ----

// 3 bit gun from inverted RAM bits, MAME compute_resistor_weights()
// for 1k / 470 / 270 ohm
static const uint8_t GUN_LEVEL[8] = {0, 37, 79, 117, 138, 176, 218, 255};

// palette RAM pair: lo b0 = red bit 2, hi b7-6 red bits 1-0, b5-3 green,
// b2-0 blue (bit 2 first); RGB565 byte-swapped for SPI
uint16_t elevator::pen(uint8_t lo, uint8_t hi) {
  const uint8_t r = GUN_LEVEL[((~hi >> 6) & 1) | ((~hi >> 6) & 2) | ((~lo << 2) & 4)];
  const uint8_t g = GUN_LEVEL[(~hi >> 3) & 7];
  const uint8_t b = GUN_LEVEL[~hi & 7];
  const uint16_t v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  return (uint16_t)((v << 8) | (v >> 8));
}

void elevator::prepare_frame(void) {
  vblank.read(video);
  for (uint8_t i = 0; i < PENS; i++) {
    palette[i] = pen(video.pal[2 * i], video.pal[2 * i + 1]);
  }
}

void IRAM_ATTR elevator::render_row(short row) {
}

#ifdef LED_PIN
void elevator::menuLeds(CRGB *leds) {
  defaultMenuLeds(leds);
}

void elevator::gameLeds(CRGB *leds) {
  menuLeds(leds);
}
#endif

#endif // GAME_ELEVATOR
