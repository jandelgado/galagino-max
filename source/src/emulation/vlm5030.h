#ifndef VLM5030_H
#define VLM5030_H

#include <stdint.h>

// Sanyo VLM5030 speech synthesizer, port of MAME devices/sound/vlm5030.cpp:
// LPC frames from a speech ROM, 4 interpolation steps per frame, 10 stage
// lattice filter. The caller drives the clock: one sample() per 440 chip
// clocks (8136 Hz at 3.579545 MHz), like S14001A::clock().
//
//   data_w(param), RST high -> low   latch speed / pitch parameters
//   data_w(phrase), ST high          BSY on
//   ST low                           speak phrase (address from the table
//                                    at ROM 0, 2 bytes per phrase)
//
// Indirect (phrase table) addressing only: VCU stays low on Yie Ar
// Kung-Fu and Track & Field. Noise comes from an LFSR, MAME uses rand().
class Vlm5030 {
public:
  void reset(const uint8_t *rom, uint16_t size); // size: power of two

  void data_w(uint8_t v) { latch = v; }
  void rst_w(bool level);
  void st_w(bool level);
  bool bsy() const { return pin_bsy; }

  int16_t sample(); // filter output, 10 bit scaled to int16

private:
  static const uint8_t NUM_K = 10;

  enum Phase : uint8_t { IDLE, RUN, STOP };

  void chip_reset();
  void setup_parameter(uint8_t param);
  uint8_t read_byte(uint16_t addr) const { return rom[addr & rom_mask]; }
  int get_bits(int sbit, int bits) const;
  int parse_frame();
  void load_frame();
  void next_step();
  int32_t excitation();
  int32_t lattice(int32_t v);

  const uint8_t *rom = nullptr;
  uint16_t rom_mask = 0;

  // pins and parameters
  bool pin_bsy = false, pin_st = false, pin_rst = false;
  uint8_t latch = 0;
  uint8_t interp_step = 1;
  uint8_t frame_size = 0;
  int8_t pitch_offset = 0;

  // frame sequencer
  Phase phase = IDLE;
  uint16_t address = 0;
  int16_t interp_count = 0;
  int16_t sample_count = 0;
  int16_t pitch_count = 0;

  // parameters: previous frame, interpolation target, new frame, current
  int16_t old_energy = 0, old_pitch = 0, old_k[NUM_K] = {};
  int16_t target_energy = 0, target_pitch = 0, target_k[NUM_K] = {};
  int16_t new_energy = 0, new_pitch = 0, new_k[NUM_K] = {};
  int16_t current_energy = 0, current_pitch = 0, current_k[NUM_K] = {};

  int32_t x[NUM_K] = {};
  uint16_t rng = 1;
};

#endif
