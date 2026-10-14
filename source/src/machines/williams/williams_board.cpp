#include "williams_board.h"
#include <string.h>

// IN2 bits (same on all three games)
static const uint8_t IN2_AUTO_UP = 0x01, IN2_ADVANCE = 0x02, IN2_COIN1 = 0x10;
static const int VA11_PERIOD = 32, VA11_LAST = 224, COUNT240_LINE = 240;
static const uint8_t PIA_INPUTS = 0, PIA_SYSTEM = 1;
static const uint8_t CMOS_HIGH_BITS = 0xf0;  // 4 bit CMOS reads 1 in b4-7
static const uint32_t IRQ_ENTRY_CYCLES = 19;  // push entire state + vector

WilliamsBoard::WilliamsBoard(const BoardConfig &c, uint8_t *r, uint8_t *scanA, uint8_t *scanB,
                             const uint8_t *soundRom)
  : ram(r), cfg(c), soundRomPtr(soundRom) {
  scan.attach(scanA, scanB);
}

void WilliamsBoard::bindMemory(uint8_t *cmosRam, uint8_t *soundRam) {
  cmos = cmosRam;
  snd.attach(soundRomPtr, cfg.soundRomBase, soundRam);
}

void WilliamsBoard::reset() {
  memset(ram, 0, RAM_SIZE);
  memset(cmos, 0, cfg.cmosMask + 1);  // cold boot: the Advance pulse restores factory settings
  memset(palette, 0, sizeof(palette));
  scan.clear();
  pia[PIA_INPUTS].reset();
  pia[PIA_SYSTEM].reset();
  snd.reset();
  cycles = stall = frame = 0;
  keyState = 0;
  memset(&cpu6809, 0, sizeof(cpu6809));
  m6809_reset(&cpu6809);
  installRomWindow();  // m6809_reset() cleared it
}

bool WilliamsBoard::booted() const { return frame >= cfg.advanceFrame + ADVANCE_FRAMES; }
void WilliamsBoard::setKeys(unsigned int k) { keyState = k; }
unsigned int WilliamsBoard::keys() const { return booted() ? keyState : 0; }
m6809_state *WilliamsBoard::cpu() { return &cpu6809; }
const Scanout &WilliamsBoard::scanout() const { return scan; }
WilliamsSound &WilliamsBoard::sound() { return snd; }
uint8_t WilliamsBoard::ramByte(uint16_t addr) const { return ram[addr]; }
void WilliamsBoard::addStall(uint32_t c) { stall += c; }

// One frame in 4-line slices: beam inputs, 6809, 6808, scanout copy
void WilliamsBoard::runFrame() {
  for (int line = 0; line < LINES; line += SLICE_LINES) {
    beam(line);
    runMain((line + SLICE_LINES) * CYCLES_PER_LINE);
    snd.run(SLICE_LINES * CYCLES_PER_LINE);
    scan.capture(ram, cfg.firstByteCol, line, SLICE_LINES, palette);
  }
  scan.flip();
  cycles -= FRAME_CYCLES;  // keep the overshoot of the last instruction
  frame++;
}

// VA11 into PIA1 CB1 every 32 lines (not at 256), COUNT240 into CA1
void WilliamsBoard::beam(int line) {
  if (line % VA11_PERIOD == 0 && line <= VA11_LAST) {
    pia[PIA_SYSTEM].cb1(line & VA11_PERIOD);
  }
  if (line == 0) {
    pia[PIA_SYSTEM].ca1(false);
  } else if (line == COUNT240_LINE) {
    pia[PIA_SYSTEM].ca1(true);
  }
}

// The PIA IRQ is a level: m6809_irq() per instruction while it is high
// (the core only takes an IRQ inside m6809_irq(), masked ones stay pending)
void WilliamsBoard::runMain(uint32_t until) {
  while (cycles < until) {
    if (pia[PIA_SYSTEM].irq()) {
      const uint16_t pc = cpu6809.PC;
      m6809_irq(&cpu6809);
      if (cpu6809.PC != pc) {
        cycles += IRQ_ENTRY_CYCLES;  // m6809_step() resets the core's count
      }
    } else {
      cpu6809.irq_pending = 0;
    }
    cycles += m6809_step(&cpu6809, 1);
    cycles += stall;
    stall = 0;
  }
}

uint8_t WilliamsBoard::videoCounter() const {
  const uint32_t line = cycles / CYCLES_PER_LINE;
  return line < 0x100 ? (line & 0xfc) : 0xfc;
}

uint8_t WilliamsBoard::in2() {
  uint8_t v = cfg.autoUp ? IN2_AUTO_UP : 0;
  // zeroed CMOS: the game waits for Advance to restore factory settings
  if (frame >= cfg.advanceFrame && frame < cfg.advanceFrame + ADVANCE_FRAMES) {
    v |= IN2_ADVANCE;
  }
  if (keys() & BOARD_KEY_COIN) {
    v |= IN2_COIN1;
  }
  return v;
}

uint8_t WilliamsBoard::piaRead(uint8_t index, uint8_t reg) {
  if (index == PIA_INPUTS) {
    pia[index].setInputs(in0(), in1());  // per read: Joust's mux follows CB2
  } else {
    pia[index].setInputs(in2(), 0xff);
  }
  return pia[index].read(reg);
}

void WilliamsBoard::piaWrite(uint8_t index, uint8_t reg, uint8_t val) {
  if (pia[index].write(reg, val) == Pia6821::PORT_B && index == PIA_SYSTEM) {
    snd.command(pia[index].outB());
  }
}

void WilliamsBoard::paletteWrite(uint8_t index, uint8_t val) { palette[index & (PENS - 1)] = val; }
uint8_t WilliamsBoard::cmosRead(uint16_t addr) const { return cmos[addr & cfg.cmosMask]; }
void WilliamsBoard::cmosWrite(uint16_t addr, uint8_t val) {
  cmos[addr & cfg.cmosMask] = val | CMOS_HIGH_BITS;
}

uint8_t WilliamsBoard::readLow(uint16_t addr) { return ram[addr]; }

uint8_t WilliamsBoard::read(uint16_t addr) {
  return addr < RAM_SIZE ? readLow(addr) : readHigh(addr);
}

void WilliamsBoard::write(uint16_t addr, uint8_t val) {
  if (addr < RAM_SIZE) {
    ram[addr] = val;  // ROM overlays never block writes
    return;
  }
  writeHigh(addr, val);
}
