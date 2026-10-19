#ifndef B1_BOARD_H
#define B1_BOARD_H

#include "williams_board.h"
#include "williams_blitter.h"

struct B1Map {
  uint16_t fixedBase;    // fixed ROM 0xd000 (Robotron, Joust) / 0xe000 (Sinistar)
  uint16_t clipAddress;  // blitter window end (0xc900 b2), 0 = no window
};
static const B1Map B1_STANDARD = { 0xd000, 0 };
static const B1Map B1_SINISTAR = { 0xe000, 0x7400 };

// Williams board with SC1 blitter (Robotron, Joust, Sinistar): ROM
// 0x0000-0x8fff overlays RAM reads while 0xc900 b0 = 1; fixed ROM from
// map.fixedBase. rom: 0x0000-0x8fff, then the fixed ROM at offset 0x9000.
// Sinistar adds 4 KB SRAM at 0xd000 and the blitter window.
class B1Board : public WilliamsBoard {
public:
  B1Board(const BoardConfig &cfg, uint8_t *ram, uint8_t *scanA, uint8_t *scanB,
          const uint8_t *rom, const uint8_t *soundRom, const B1Map &map = B1_STANDARD);

  void bindSram(uint8_t *sram4k);  // 0xd000-0xdfff (Sinistar); before the CPU runs

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
    // window (Sinistar): VRAM from clipAddress up is write protected for
    // the blitter only; SRAM above 0xc000 is never clipped
    void write(uint16_t a, uint8_t v) {
      if (a < RAM_SIZE) {
        if (!b.windowed || a < b.map.clipAddress) {
          b.ram[a] = v;
        }
      } else if (b.isSram(a)) {
        b.sram[a - SRAM_BASE] = v;
      }
    }
  };

  static const uint16_t BANK_END = 0x9000, FIXED_OFS = 0x9000;
  static const uint16_t SRAM_BASE = 0xd000, SRAM_SIZE = 0x1000;

  bool isSram(uint16_t a) const { return sram && a >= SRAM_BASE && a < SRAM_BASE + SRAM_SIZE; }

  const uint8_t *rom;
  B1Map map;
  uint8_t *sram = nullptr;
  bool romSelected = false;
  bool windowed = false;
  Sc1Blitter blitter;
};

#endif
