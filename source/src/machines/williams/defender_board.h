#ifndef DEFENDER_BOARD_H
#define DEFENDER_BOARD_H

#include "williams_board.h"
#include "defender_controls.h"

// Defender: no blitter, I/O and 7 ROM pages share 0xc000-0xcfff (bank
// select written to 0xd000-0xdfff, 0 = I/O).
class DefenderBoard : public WilliamsBoard {
public:
  static const BoardConfig CONFIG;
  DefenderBoard(uint8_t *ram, uint8_t *scanA, uint8_t *scanB, const uint8_t *mainRom,
                const uint8_t *bankedRom, const uint8_t *soundRom, unsigned int actionKey);

protected:
  uint8_t readHigh(uint16_t addr) override;
  void writeHigh(uint16_t addr, uint8_t val) override;
  uint8_t in0() override;
  uint8_t in1() override;
  void installRomWindow() override;
  void frameInputs() override;

private:
  static const uint16_t ROM_BASE = 0xd000, ROM_SIZE = 0x3000, PAGE_SIZE = 0x1000;
  static const uint8_t PAGES = 7, BANK_MASK = 0x0f;

  uint8_t readIo(uint16_t addr);
  void writeIo(uint16_t addr, uint8_t val);

  const uint8_t *mainRom, *bankedRom;
  unsigned int actionKey;
  DefenderControls controls;
  uint16_t inputs = 0;     // IN0 | IN1 << 8
  uint8_t bank = 0;
};

#endif
