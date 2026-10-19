#ifndef B1_BOARD_H
#define B1_BOARD_H

#include "williams_board.h"
#include "williams_blitter.h"

// Williams board with SC1 blitter (Robotron, Joust): ROM 0x0000-0x8fff
// overlays RAM reads while 0xc900 b0 = 1; fixed ROM 0xd000-0xffff.
// rom: 0x0000-0x8fff, then 0xd000-0xffff at offset 0x9000.
class B1Board : public WilliamsBoard {
public:
  B1Board(const BoardConfig &cfg, uint8_t *ram, uint8_t *scanA, uint8_t *scanB,
          const uint8_t *rom, const uint8_t *soundRom);

protected:
  uint8_t readLow(uint16_t addr) override;
  uint8_t readHigh(uint16_t addr) override;
  void writeHigh(uint16_t addr, uint8_t val) override;
  void installRomWindow() override;

private:
  // blitter bus: CPU view for reads, RAM below 0xc000 for writes
  struct Bus {
    B1Board &b;
    uint8_t read(uint16_t a) { return b.read(a); }
    uint8_t readVram(uint16_t a) { return a < RAM_SIZE ? b.ram[a] : b.read(a); }
    void write(uint16_t a, uint8_t v) {
      if (a < RAM_SIZE) {
        b.ram[a] = v;
      }
    }
  };

  static const uint16_t BANK_END = 0x9000, FIXED_BASE = 0xd000, FIXED_OFS = 0x9000;

  const uint8_t *rom;
  bool romSelected = false;
  Sc1Blitter blitter;
};

#endif
