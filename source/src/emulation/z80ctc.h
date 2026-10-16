#ifndef Z80CTC_H
#define Z80CTC_H

#include <stdint.h>

// Zilog Z80 CTC (MAME z80ctc.cpp): 4 timer/counter channels, IM 2 vectors,
// daisy-chain priority ch0 > ch3. ack() clears the pending bit; there is no
// in-service lock (RETI is not tracked).
class Z80Ctc {
public:
  void reset() {
    for (int i = 0; i < 4; i++) {
      ch[i] = Channel();
    }
    pending = 0;
    vector = 0;
  }

  void write(uint8_t n, uint8_t v) {
    Channel &c = ch[n];
    if (c.wantTc) {
      // a time constant (0 = 256) restarts the channel
      c.tc = v;
      c.wantTc = false;
      c.mode &= ~RESET;
      c.down = c.tc ? c.tc : 256;
      c.prescale = 0;
      c.running = (c.mode & COUNTER) || !(c.mode & WAIT_TRG);
      return;
    }
    if (!(v & CONTROL)) {
      if (n == 0) {
        vector = v & 0xf8;
      }
      return;
    }
    c.mode = v;
    if (!(v & IRQ_EN)) {
      pending &= ~(1 << n);
    }
    if (v & RESET) {
      c.running = false;  // a pending IRQ survives a reset
    }
    if (v & TC_NEXT) {
      c.wantTc = true;
    }
  }

  uint8_t read(uint8_t n) const { return (uint8_t)ch[n].down; }

  // CLK/TRG edge: counts in counter mode, starts a waiting timer
  void trigger(uint8_t n) {
    Channel &c = ch[n];
    if (c.wantTc) {
      return;
    }
    if (c.mode & COUNTER) {
      if (c.running) {
        count(n);
      }
    } else if (!c.running && (c.mode & WAIT_TRG) && !(c.mode & RESET)) {
      c.running = true;
    }
  }

  // advance running timers by CPU cycles
  void tick(int cycles) {
    for (int n = 0; n < 4; n++) {
      Channel &c = ch[n];
      if (!c.running || (c.mode & COUNTER)) {
        continue;
      }
      const int div = (c.mode & PRESCALE256) ? 256 : 16;
      c.prescale += cycles;
      while (c.prescale >= div) {
        c.prescale -= div;
        count(n);
      }
    }
  }

  bool irq() const { return pending != 0; }

  // highest-priority pending channel: clear it and return its vector
  uint8_t ack() {
    for (int n = 0; n < 4; n++) {
      if (pending & (1 << n)) {
        pending &= ~(1 << n);
        return vector + n * 2;
      }
    }
    return vector;
  }

  // zero count since the last call (ZC/TO output, e.g. ch0 -> trg1)
  bool zc(uint8_t n) {
    const bool z = ch[n].zc;
    ch[n].zc = false;
    return z;
  }

private:
  static const uint8_t CONTROL = 0x01, RESET = 0x02, TC_NEXT = 0x04,
                       WAIT_TRG = 0x08, PRESCALE256 = 0x20, COUNTER = 0x40,
                       IRQ_EN = 0x80;

  struct Channel {
    uint8_t mode = RESET | CONTROL;
    uint8_t tc = 0;
    int down = 256;
    int prescale = 0;
    bool running = false;
    bool wantTc = false;
    bool zc = false;
  };

  void count(int n) {
    Channel &c = ch[n];
    if (--c.down > 0) {
      return;
    }
    c.down = c.tc ? c.tc : 256;
    c.zc = true;
    if (c.mode & IRQ_EN) {
      pending |= 1 << n;
    }
  }

  Channel ch[4];
  uint8_t pending = 0;
  uint8_t vector = 0;
};

#endif
