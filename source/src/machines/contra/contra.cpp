#include "../../catalog.h"
#ifdef GAME_CONTRA

#include "contra.h"
#include "contra_rom_main.h"
#include "contra_prom.h"

// one frame = 264 lines of 384 px at 6 MHz, CPU 3 MHz: 50688 cycles
static constexpr int LINES = 264;
static constexpr int CYCLES_LINE = 192;
static constexpr int VBLANK_LINE = 240;   // K007121 IRQ and sprite latch

static constexpr uint16_t REGS_END = 0x0100;
static constexpr uint16_t BANK_BASE = 0x6000, FIXED_BASE = 0x8000;
static constexpr uint32_t BANK_SIZE = 0x2000;
static constexpr uint32_t FIXED_ROM = 0x18000;   // 8000-ffff = ROM 18000-1ffff
static constexpr uint8_t BANK_MASK = 0x0f;
static constexpr uint16_t BANK_SELECT = 0x7000;

static constexpr uint8_t CTRL_IRQ = 7;           // b1 IRQ enable, 0 acknowledges
static constexpr uint8_t CTRL7_IRQ_ENABLE = 0x02;

// main CPU inputs (konamipt.h KONAMI8_SYSTEM_UNK / KONAMI8_B12_UNK), active low
static constexpr uint8_t SYS_COIN1 = 0x01, SYS_START1 = 0x08;
static constexpr uint8_t P1_LEFT = 0x01, P1_RIGHT = 0x02, P1_UP = 0x04, P1_DOWN = 0x08;
static constexpr uint8_t P1_FIRE = 0x10, P1_JUMP = 0x20;

// jump on the START button: BUTTON_START with a COIN pin (EXTRA is then
// COIN) or the galagino controller, else START itself reports as
// BUTTON_EXTRA (BUTTON_START only follows a virtual coin), as gng
#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int JUMP_KEY = BUTTON_START;
#else
static const unsigned int JUMP_KEY = BUTTON_EXTRA;
#endif

contra::contra() : contra_main_rom(contra_main_rom_blob), contra_lut(contra_lut_blob) {
  // m6809_reset() reads the reset vector through main_rom
  main_rom = contra_main_rom.data();
  ram = Arena::alloc<uint8_t>(CONTRA_RAM_END);
}

void contra::reset() {
  machineBase::reset();
  memset(ram, 0, CONTRA_RAM_END);
  memset(ctrl, 0, sizeof(ctrl));
  memset(math_regs, 0, sizeof(math_regs));
  product = quotient = remainder = 0;
  bank = 0;
  irq_line = false;

  m6809_reset(&main_cpu);
  main_cpu.rom_direct = main_rom + FIXED_ROM;   // must follow m6809_reset()
  main_cpu.rom_base = FIXED_BASE;
  main_cpu.rom_size = 0x10000 - FIXED_BASE;
}

// ---- Main CPU ----

unsigned char GAME_IRAM contra::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

unsigned char GAME_IRAM contra::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= FIXED_BASE) {
    return main_rom[FIXED_ROM + addr - FIXED_BASE];
  }
  if (addr >= BANK_BASE) {
    return main_rom[bank * BANK_SIZE + addr - BANK_BASE];
  }
  if (addr < REGS_END) {
    return readPort(addr);
  }
  return ram[addr];
}

uint8_t contra::readPort(uint16_t addr) {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xff;
  switch (addr) {
    case 0x10:
      if (keys & BUTTON_COIN) {
        v &= ~SYS_COIN1;
      }
      if (keys & BUTTON_START) {
        v &= ~SYS_START1;
      }
      return v;
    case 0x11:
      if (keys & BUTTON_LEFT) {
        v &= ~P1_LEFT;
      }
      if (keys & BUTTON_RIGHT) {
        v &= ~P1_RIGHT;
      }
      if (keys & BUTTON_UP) {
        v &= ~P1_UP;
      }
      if (keys & BUTTON_DOWN) {
        v &= ~P1_DOWN;
      }
      if (keys & (BUTTON_FIRE | BUTTON_A | BUTTON_L1)) {
        v &= ~P1_FIRE;
      }
      if (keys & (JUMP_KEY | BUTTON_B | BUTTON_R1)) {
        v &= ~P1_JUMP;
      }
      return v;
    case 0x12:
      return 0xff;   // player 2
    case 0x14:
      return CONTRA_DSW1;
    case 0x15:
      return CONTRA_DSW2 | (input->demoSoundsOff() ? CONTRA_DSW2_DEMO_SOUND_OFF : 0);
    case 0x16:
      return CONTRA_DSW3;
  }
  if (addr >= 0x08 && addr < 0x10) {
    return readMath(addr - 0x08);
  }
  return ram[addr];   // K007121 scroll RAM 20-5f, 80-bf
}

void GAME_IRAM contra::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr == BANK_SELECT) {
    bank = val & BANK_MASK;
    return;
  }
  if (addr >= CONTRA_RAM_END) {
    return;
  }
  ram[addr] = val;
  if (addr >= REGS_END) {
    return;
  }

  if (addr < 0x08) {
    writeCtrl(0, addr, val);
  } else if (addr < 0x10) {
    writeMath(addr - 0x08, val);
  } else if (addr >= 0x60 && addr < 0x68) {
    writeCtrl(1, addr - 0x60, val);
  } else if (addr == 0x1c) {
    game_started = 1;   // first sound command: attract runs
  }
  // 18 coin counter, 1a sound IRQ, 1e watchdog: ignored
}

void contra::writeCtrl(int chip, uint8_t reg, uint8_t val) {
  // k007121.cpp ctrl_w: clearing the enable bit acknowledges the IRQ
  if (chip == 0 && reg == CTRL_IRQ && !(val & CTRL7_IRQ_ENABLE)) {
    irq_line = false;
    main_cpu.irq_pending = 0;
  }
  ctrl[chip][reg] = val;
}

// ---- 007452 math chip (k007452.cpp) ----

void contra::writeMath(uint8_t reg, uint8_t val) {
  if (reg >= MATH_REGS) {
    return;
  }
  math_regs[reg] = val;

  if (reg == 1) {
    product = (uint16_t)(math_regs[0] * math_regs[1]);
    return;
  }
  if (reg != 5) {
    return;
  }
  const uint16_t dividend = (uint16_t)(math_regs[4] << 8 | math_regs[5]);
  const uint16_t divisor = (uint16_t)(math_regs[2] << 8 | math_regs[3]);
  quotient = divisor ? dividend / divisor : 0xffff;
  remainder = divisor ? dividend % divisor : 0;
}

uint8_t contra::readMath(uint8_t reg) {
  switch (reg) {
    case 0: return product & 0xff;
    case 1: return product >> 8;
    case 2: return remainder & 0xff;
    case 3: return remainder >> 8;
    case 4: return quotient & 0xff;
    case 5: return quotient >> 8;
  }
  return 0;
}

// ---- Frame ----

// 264 lines; the IRQ is a level (k007121.cpp scanline()): asserted at line
// 240 while enabled, re-offered each line until the game clears it
void contra::run_frame(void) {
  int debt = 0;
  for (int line = 0; line < LINES; line++) {
    if (line == VBLANK_LINE && (ctrl[0][CTRL_IRQ] & CTRL7_IRQ_ENABLE)) {
      irq_line = true;
    }
    if (irq_line) {
      m6809_irq(&main_cpu);
    }

    debt += CYCLES_LINE;
    while (debt > 0) {
      const int c = m6809_step(&main_cpu, 4);
      debt -= c > 0 ? c : 1;
    }
  }
}

#ifdef LED_PIN
// logo colors: red, rust, beige
static const CRGB CONTRA_LED_RED(0xa50000), CONTRA_LED_RUST(0x843110), CONTRA_LED_BEIGE(0xb8ac91);

void contra::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      CONTRA_LED_BEIGE, CONTRA_LED_RUST, CONTRA_LED_RED, CONTRA_LED_RED,
      CONTRA_LED_RED, CONTRA_LED_RUST, CONTRA_LED_BEIGE};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// red while FIRE is held
void contra::gameLeds(CRGB *leds) {
  if (input->buttons_get() & BUTTON_FIRE) {
    fill_solid(leds, NUM_LEDS, CONTRA_LED_RED);
    return;
  }
  menuLeds(leds);
}
#endif

#endif // GAME_CONTRA
