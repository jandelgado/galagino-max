#ifndef RIOT6532_H
#define RIOT6532_H

#include <stdint.h>
#include <string.h>

// MOS 6532 RIOT (MAME machine/mos6530.cpp), sound boards of Star Wars and Q*bert:
// 128 bytes RAM, ports, interval timer and PA7 edge detect, both raising
// the sound CPU IRQ. Time is the sound CPU cycle count, same clock.
class Riot6532 {
public:
  static const uint8_t RAM_SIZE = 0x80;

  void reset() {
    memset(this, 0, sizeof(*this));
  }

  uint8_t ram_read(uint8_t a) const { return ram[a & (RAM_SIZE - 1)]; }
  void ram_write(uint8_t a, uint8_t v) { ram[a & (RAM_SIZE - 1)] = v; }

  // port A as seen by a read: outputs where DDR is set, else pa_in
  uint8_t port_a(uint8_t pa_in) const { return (pa_out & pa_ddr) | (pa_in & ~pa_ddr); }
  uint8_t port_b(uint8_t pb_in) const { return (pb_out & pb_ddr) | (pb_in & ~pb_ddr); }

  // I/O registers, offset 0x00-0x1f (MAME mos6532_device::io_map)
  uint8_t io_read(uint8_t offset, uint8_t pa_in, uint8_t pb_in, uint32_t now) {
    switch (offset & 0x05) {
      case 0x00: return (offset & 2) ? port_b(pb_in) : port_a(pa_in);
      case 0x01: return (offset & 2) ? pb_ddr : pa_ddr;
      case 0x04: return timer_read(offset & 0x08, now);
      default: return irq_flags_read();
    }
  }

  void io_write(uint8_t offset, uint8_t v, uint8_t pa_in, uint32_t now) {
    offset &= 0x1f;
    if (offset & 0x04) {
      if (offset & 0x10) {
        timer_write(offset & 3, v, offset & 0x08, now);
      } else {
        edge_control(offset);
      }
      return;
    }

    switch (offset & 3) {
      case 0: pa_out = v; edge_detect(pa_in); break;
      case 1: pa_ddr = v; edge_detect(pa_in); break;
      case 2: pb_out = v; break;
      case 3: pb_ddr = v; break;
    }
  }

  // pin levels the devices see: undriven pins float high (MAME update_pa)
  uint8_t pa_output() const { return (pa_out & pa_ddr) | ~pa_ddr; }
  uint8_t pb_output() const { return (pb_out & pb_ddr) | ~pb_ddr; }

  // port A inputs changed (latch flags): PA7 edge detect
  void edge_detect(uint8_t pa_in) {
    const bool state = port_a(pa_in) & 0x80;
    if (state != pa7 && state == pa7_dir && !irq_edge) {
      irq_edge = true;
    }
    pa7 = state;
  }

  // call before each sound CPU instruction; true: IRQ line asserted
  bool irq(uint32_t now) {
    if (counting && (int32_t)(now - timer_end) >= 0) {
      counting = false;
      irq_timer = true;
    }
    return (ie_timer && irq_timer) || (ie_edge && irq_edge);
  }

  // cycles from now until the timer raises the IRQ line, UINT32_MAX: not
  // counting or disabled. Edge IRQs come from latch writes, irq() sees them.
  uint32_t until_timer_irq(uint32_t now) const {
    if (!counting || !ie_timer) {
      return UINT32_MAX;
    }
    const int32_t remain = (int32_t)(timer_end - now);
    return remain > 0 ? remain : 0;
  }

private:
  enum : uint8_t { IRQ_TIMER = 0x80, IRQ_EDGE = 0x40 };

  // timer value: counts down from the written value every 1 << shift
  // cycles, after 0 at every cycle from 0xff
  uint8_t timer_value(uint32_t now) const {
    const int32_t remain = (int32_t)(timer_end - now);
    if (remain > 0) {
      const uint32_t val = remain >> shift;
      return (remain & ((1 << shift) - 1)) ? val : val - 1;
    }
    return 0xff - ((uint32_t)-remain & 0xff);
  }

  uint8_t timer_read(bool ie, uint32_t now) {
    irq(now);
    const uint8_t v = timer_value(now);
    if (irq_timer) {
      // back to counting with the prescaler, from the current value
      irq_timer = false;
      if (!counting) {
        start(v, now);
      }
    }
    ie_timer = ie;
    return v;
  }

  void timer_write(uint8_t prescale, uint8_t v, bool ie, uint32_t now) {
    static const uint8_t SHIFTS[4] = {0, 3, 6, 10};
    shift = SHIFTS[prescale];
    start(v, now);
    irq_timer = false;
    ie_timer = ie;
  }

  void start(uint8_t v, uint32_t now) {
    timer_end = now + 1 + ((uint32_t)v << shift);
    counting = true;
  }

  uint8_t irq_flags_read() {
    const uint8_t v = (irq_timer ? IRQ_TIMER : 0) | (irq_edge ? IRQ_EDGE : 0);
    irq_edge = false;
    return v;
  }

  void edge_control(uint8_t offset) {
    pa7_dir = offset & 1;
    ie_edge = offset & 2;
  }

  uint8_t ram[RAM_SIZE];
  uint8_t pa_out, pa_ddr, pb_out, pb_ddr;
  uint32_t timer_end; // cycle the count passes 0
  uint8_t shift;
  bool counting, irq_timer, ie_timer;
  bool irq_edge, ie_edge, pa7, pa7_dir;
};

#endif
