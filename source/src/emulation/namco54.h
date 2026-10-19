#ifndef NAMCO54_H
#define NAMCO54_H

#include <atomic>
#include <stdint.h>

#include "../cpus/mb88xx/mb88xx.h"

// Namco 54xx explosion generator: MB8844 MCU running the 54xx ROM, its
// three 4-bit outputs filtered by the galaga/xevious discrete network
// (MAME namco54.cpp, galaga_a.cpp galaga_discrete).
//
//   emulation core                audio core, per 24 kHz sample
//   06xx data write: write() ---> render(): next byte -> IRQ pulse,
//   06xx reset line: hold()       MCU ~10.7 cycles, outputs -> filters
//
// The 06xx pulses the chip select once per clock period, so bytes reach
// the MCU at the 06xx rate, not the burst rate of the emulated frame.
//
// rom: the 1 KB 54xx program (ships in galaga.zip, see
// machines/namco54/namco54_program.h).
class Namco54 : public Mb88 {
public:
  explicit Namco54(const uint8_t *rom);

  // emulation core. ctrl: 06xx control register, bits 7-5 = clock divide
  void write(uint8_t data, uint8_t ctrl);
  void hold(bool reset);

  // audio core: next sample on galagino's +-512 scale
  int16_t render();

private:
  uint8_t read_k() override { return cmd >> 4; }
  uint8_t read_r(uint8_t port) override { return port == 0 ? (cmd & 0x0f) : 0; }
  void write_o(uint8_t value, uint8_t mask) override;
  void write_r(uint8_t port, uint8_t value) override;

  // one byte from the queue, raising the IRQ for half a 06xx period
  void feed(int cycles);

  // galaga_discrete: 4-bit R ladder DAC -> op-amp band pass, 3 channels
  struct BandPass {
    float b0, a1, a2;      // b1 = 0, b2 = -b0 (MAME filter2 band pass)
    float x1, x2, y1, y2;
    void set(float r_in, float r_bias, float r_f, float c);
    float step(float v_dac);
  };
  float mix(float c1, float c2, float c3);

  static const uint8_t QUEUE_SIZE = 64;
  std::atomic<uint8_t> head{0};
  std::atomic<uint8_t> tail{0};
  uint16_t queue[QUEUE_SIZE] = {}; // data | clock divide shift << 8
  std::atomic<bool> reset_line{false};

  // audio core only
  uint8_t cmd = 0;
  uint8_t out[3] = {};             // 0: O3-O0, 1: O7-O4, 2: R1
  uint32_t phase = 0;
  int gap = 0, irq_left = 0;
  bool in_reset = false;
  float dac[16];
  BandPass chanl[3];               // fed by out[2], out[1], out[0]
  float hp = 0, hp_k;              // mixer output coupling cap
};

#endif
