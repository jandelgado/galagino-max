#ifndef TMS5220_H
#define TMS5220_H

#include <stdint.h>
#include "speech_ring.h"

// TI TMS5220 speech synthesizer, port of MAME devices/sound/tms5220.cpp
// (TMS5220 variant): 16 byte FIFO fed by Speak External, LPC frame
// parser, interpolation and 10 stage lattice filter, analog output clip.
// The /RS /WS ready timing is MAME's "true timing" mode. No speech ROM
// (VSM) commands, no IRQ pin, no 5220C rate control.
//
//   CPU: data_w(), /WS low --(16 clocks)--> byte into FIFO, READY
//   CPU: /RS low           --( 9 clocks)--> status latched, READY
//   update(now): one sample per 80 chip clocks -> SpeechRing
//
// Time is the caller's cycle count; all calls pass a monotonic now.
class Tms5220 {
public:
  // cycles_per_sample: caller cycles per 80 chip clocks, at most 4095
  void reset(SpeechRing *out, uint16_t cycles_per_sample, uint32_t now);

  void data_w(uint8_t v) { write_latch = v; }
  void wsq_w(bool level, uint32_t now);
  void rsq_w(bool level, uint32_t now);
  uint8_t status_r(uint32_t now); // read latch in an /RS cycle, else 0xff
  bool readyq_r(uint32_t now);    // true: busy

  // samples up to cycle now
  void update(uint32_t now);

private:
  static const uint8_t FIFO_SIZE = 16;
  static const uint8_t NUM_K = 10;

  void chip_reset();
  void strobe(uint8_t next_rs_ws, uint8_t clocks, uint32_t now);
  void ready_cycle(uint8_t clocks, uint32_t now);
  void io_ready_event();
  void data_write(uint8_t data);
  void process_command(uint8_t cmd);
  void fifo_status();
  int extract_bits(int count);
  uint8_t status_read() const;
  void parse_frame();
  void zero_new_frame();
  int16_t process();
  int32_t lattice_filter();

  bool talk_status() const { return spen || talkd; }
  bool new_frame_stop() const { return new_energy_idx == 0x0f; }
  bool new_frame_silence() const { return new_energy_idx == 0; }
  bool new_frame_unvoiced() const { return new_pitch_idx == 0; }

  SpeechRing *ring;
  uint16_t cycles_per_sample;
  uint32_t next_sample;   // caller cycle of the next sample
  uint32_t ready_at;      // caller cycle READY returns, if ready_pending
  bool ready_pending;

  // FIFO
  uint8_t fifo[FIFO_SIZE];
  uint8_t fifo_head, fifo_tail, fifo_count, fifo_bits_taken;

  // status and control
  bool previous_talk_status, spen, ddis, talk, talkd;
  bool buffer_low, buffer_empty;
  bool io_ready;
  uint8_t rs_ws;          // /RS bit 1, /WS bit 0
  uint8_t read_latch, write_latch;

  // frames
  bool olde, oldp;        // old frame silent / unvoiced
  uint8_t new_energy_idx, new_pitch_idx;
  uint8_t new_k_idx[NUM_K];

  // generator
  int16_t current_energy, current_pitch;
  int16_t current_k[NUM_K];
  uint16_t previous_energy;
  uint8_t subcycle, pc, ip;
  bool inhibit, uv_zpar, zpar, pitch_zero;
  uint16_t pitch_count;
  int32_t u[11], x[NUM_K];
  uint16_t rng;
  int16_t excitation;
};

#endif
