#ifndef WILLIAMS_BOARD_H
#define WILLIAMS_BOARD_H

#include <stdint.h>
#include "../../cpus/m6809/m6809.h"
#include "williams_keys.h"
#include "pia6821.h"
#include "williams_sound.h"
#include "williams_video.h"

struct BoardConfig {
  uint8_t firstByteCol;   // VRAM byte of visible x: 3 (x 6), Defender 6 (x 12)
  uint8_t autoUp;         // IN2 b0 during boot and play: Defender 1, Robotron/Joust 0
  uint16_t cmosMask;      // CMOS address mask: Defender 0xff, others 0x3ff
  uint16_t soundRomBase;  // 0xf800 Defender, 0xf000 others
  uint16_t advanceFrame;  // Advance pulse once the restore prompt shows (boot test)
};

// Williams gen 1 board (MAME williams/williams.cpp), host-compilable:
// 6809 @ 1 MHz run in 4-line slices, 48 KB RAM (bitmap at byte
// (x / 2) * 256 + y), PIA0 inputs, PIA1 system (IN2, beam IRQs, sound
// command), 6808 sound board, and the scanout fed as the beam passes.
// Game boards supply the 0xc000-0xffff map and the input ports.
class WilliamsBoard {
public:
  static const int LINES = 260, SLICE_LINES = 4, CYCLES_PER_LINE = 64;
  static const uint32_t FRAME_CYCLES = LINES * CYCLES_PER_LINE;  // 16640
  static const uint16_t RAM_SIZE = 0xc000;
  static const int ADVANCE_FRAMES = 20;

  WilliamsBoard(const BoardConfig &cfg, uint8_t *ram, uint8_t *scanA, uint8_t *scanB,
                const uint8_t *soundRom);
  virtual ~WilliamsBoard() {}

  // CMOS (<= 1024 B) and sound RAM (256 B) live in machineBase::memory[],
  // which init() sets after the ctor: bind before every reset()
  void bindMemory(uint8_t *cmos, uint8_t *soundRam);
  void reset();
  void runFrame();
  void setKeys(unsigned int keys);
  bool booted() const;  // factory settings committed: game runs
  uint8_t read(uint16_t addr);
  void write(uint16_t addr, uint8_t val);
  m6809_state *cpu();
  const Scanout &scanout() const;
  WilliamsSound &sound();
  uint8_t ramByte(uint16_t addr) const;

protected:
  virtual uint8_t readHigh(uint16_t addr) = 0;  // 0xc000-0xffff
  virtual void writeHigh(uint16_t addr, uint8_t val) = 0;
  virtual uint8_t readLow(uint16_t addr);       // below 0xc000 (ROM overlay)
  virtual uint8_t in0() = 0;
  virtual uint8_t in1() = 0;
  virtual void installRomWindow() = 0;          // rom_direct after m6809_reset
  virtual void frameInputs() {}                 // once per frame, before the CPU runs
  uint8_t in2();
  bool piaCb2(uint8_t index) const { return pia[index].cb2(); }
  uint8_t videoCounter() const;
  uint8_t piaRead(uint8_t index, uint8_t reg);
  void piaWrite(uint8_t index, uint8_t reg, uint8_t val);
  void paletteWrite(uint8_t index, uint8_t val);
  uint8_t cmosRead(uint16_t addr) const;
  void cmosWrite(uint16_t addr, uint8_t val);
  void addStall(uint32_t cycles);
  unsigned int keys() const;

  uint8_t *ram;
  m6809_state cpu6809;

private:
  static const int PENS = 16;

  void beam(int line);
  void runMain(uint32_t until);

  BoardConfig cfg;
  Pia6821 pia[2];
  WilliamsSound snd;
  Scanout scan;
  const uint8_t *soundRomPtr;
  uint8_t *cmos = nullptr;
  uint8_t palette[PENS];
  uint32_t cycles = 0;  // 6809 cycles into the current frame
  uint32_t stall = 0;   // blitter halt, charged after the instruction
  uint32_t frame = 0;
  unsigned int keyState = 0;
};

#endif
