#include "b1_board.h"

// main_map decode (williams.cpp): palette mirror 0x3f0, PIAs mirror 0xf0
static const uint16_t PAL_MASK = 0xfc00, PAL = 0xc000;
static const uint16_t PIA_MASK = 0xff0c, PIA0 = 0xc804, PIA1 = 0xc80c;
static const uint16_t PAGE_MASK = 0xff00, ROM_SELECT = 0xc900, BLITTER = 0xca00;
static const uint16_t COUNTER = 0xcb00, CMOS_MASK = 0xfc00, CMOS = 0xcc00;
static const uint8_t ROM_SELECT_BIT = 0x01, WINDOW_BIT = 0x04;

B1Board::B1Board(const BoardConfig &cfg, uint8_t *ram, uint8_t *scanA, uint8_t *scanB,
                 const uint8_t *r, const uint8_t *soundRom, const B1Map &m)
  : WilliamsBoard(cfg, ram, scanA, scanB, soundRom), rom(r), map(m) {}

void B1Board::bindSram(uint8_t *s) { sram = s; }

void B1Board::installRomWindow() {
  romSelected = false;
  windowed = false;
  blitter.reset();
  cpu6809.rom_direct = rom + FIXED_OFS;
  cpu6809.rom_base = map.fixedBase;
  cpu6809.rom_size = 0x10000 - map.fixedBase;
}

uint8_t B1Board::readLow(uint16_t addr) {
  return (romSelected && addr < BANK_END) ? rom[addr] : ram[addr];
}

uint8_t B1Board::readHigh(uint16_t addr) {
  if (addr >= map.fixedBase) {
    return rom[FIXED_OFS + addr - map.fixedBase];
  }
  if (isSram(addr)) {
    return sram[addr - SRAM_BASE];
  }
  if ((addr & PIA_MASK) == PIA0) {
    return piaRead(0, addr & 3);
  }
  if ((addr & PIA_MASK) == PIA1) {
    return piaRead(1, addr & 3);
  }
  if ((addr & PAGE_MASK) == COUNTER) {
    return videoCounter();
  }
  if ((addr & CMOS_MASK) == CMOS) {
    return cmosRead(addr);
  }
  return 0;
}

// 0xcbff watchdog and ROM writes are ignored
void B1Board::writeHigh(uint16_t addr, uint8_t val) {
  if (isSram(addr)) {
    sram[addr - SRAM_BASE] = val;
  } else if ((addr & PAL_MASK) == PAL) {
    paletteWrite(addr & 15, val);
  } else if ((addr & PIA_MASK) == PIA0) {
    piaWrite(0, addr & 3, val);
  } else if ((addr & PIA_MASK) == PIA1) {
    piaWrite(1, addr & 3, val);
  } else if ((addr & PAGE_MASK) == ROM_SELECT) {
    romSelected = val & ROM_SELECT_BIT;
    windowed = map.clipAddress && (val & WINDOW_BIT);
  } else if ((addr & PAGE_MASK) == BLITTER) {
    Bus bus = { *this };
    addStall(blitter.write(addr & 7, val, bus));
  } else if ((addr & CMOS_MASK) == CMOS) {
    cmosWrite(addr, val);
  }
}
