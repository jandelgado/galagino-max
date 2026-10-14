#include "defender_board.h"

// zeroed CMOS: "press advance with switch set for auto" shows at frames
// ~720-780 (MAME and host boot test agree)
static const uint16_t ADVANCE_FRAME = 750;

const BoardConfig DefenderBoard::CONFIG = { 6, 1, 0xff, 0xf800, ADVANCE_FRAME };

// I/O view decode (defender_main_map): mirrors 0x3e0 / 0x300
static const uint16_t IO_PAL_MASK = 0xfc10, IO_PAL = 0xc000;
static const uint16_t IO_CMOS_MASK = 0xfc00, IO_CMOS = 0xc400, IO_COUNTER = 0xc800;
static const uint16_t IO_PIA_MASK = 0xfc18, IO_PIA = 0xcc00, IO_PIA0_BIT = 0x0004;
static const uint16_t BANK_SELECT_END = 0xe000;
static const uint8_t IN0_START1 = 0x20;

DefenderBoard::DefenderBoard(uint8_t *ram, uint8_t *scanA, uint8_t *scanB, const uint8_t *m,
                             const uint8_t *b, const uint8_t *s, unsigned int action)
  : WilliamsBoard(CONFIG, ram, scanA, scanB, s), mainRom(m), bankedRom(b), actionKey(action) {}

void DefenderBoard::installRomWindow() {
  bank = 0;
  cpu6809.rom_direct = mainRom;
  cpu6809.rom_base = ROM_BASE;
  cpu6809.rom_size = ROM_SIZE;
}

uint8_t DefenderBoard::readHigh(uint16_t addr) {
  if (addr >= ROM_BASE) {
    return mainRom[addr - ROM_BASE];
  }
  if (bank == 0) {
    return readIo(addr);
  }
  if (bank <= PAGES) {
    return bankedRom[(bank - 1) * PAGE_SIZE + (addr & (PAGE_SIZE - 1))];
  }
  return 0;  // banks 8-15 unmapped
}

uint8_t DefenderBoard::readIo(uint16_t addr) {
  if ((addr & IO_CMOS_MASK) == IO_CMOS) {
    return cmosRead(addr);
  }
  if ((addr & IO_CMOS_MASK) == IO_COUNTER) {
    return videoCounter();
  }
  if ((addr & IO_PIA_MASK) == IO_PIA) {
    return piaRead((addr & IO_PIA0_BIT) ? 0 : 1, addr & 3);
  }
  return 0;  // palette and video control are write only
}

void DefenderBoard::writeHigh(uint16_t addr, uint8_t val) {
  if (addr >= ROM_BASE) {
    if (addr < BANK_SELECT_END) {
      bank = val & BANK_MASK;
    }
    return;
  }
  if (bank == 0) {
    writeIo(addr, val);  // ROM pages ignore writes
  }
}

// video control (cocktail flip) and the watchdog are ignored
void DefenderBoard::writeIo(uint16_t addr, uint8_t val) {
  if ((addr & IO_PAL_MASK) == IO_PAL) {
    paletteWrite(addr & 15, val);
  } else if ((addr & IO_CMOS_MASK) == IO_CMOS) {
    cmosWrite(addr, val);
  } else if ((addr & IO_PIA_MASK) == IO_PIA) {
    piaWrite((addr & IO_PIA0_BIT) ? 0 : 1, addr & 3, val);
  }
}

uint8_t DefenderBoard::in0() { return (keys() & BOARD_KEY_START) ? IN0_START1 : 0; }
uint8_t DefenderBoard::in1() { return 0; }
