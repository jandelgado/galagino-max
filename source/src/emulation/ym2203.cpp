#include <string.h>
#include "ym2203.h"
#include "ym2203fm.h"

Ym2203::Ym2203(uint8_t ssg_inc, uint8_t ssg_gain, Ym2203Fm *fm)
    : fm(fm), ssg(ssg_inc, ssg_gain) {}

void Ym2203::writeAddress(uint8_t a) {
  addr = a;
  // ymfm ym2203::write_address: $2d-$2f select the prescaler
  if (a == 0x2d) {
    pre = 6;
  } else if (a == 0x2e && pre == 6) {
    pre = 3;
  } else if (a == 0x2f) {
    pre = 2;
  }
  if (fm) {
    fm->writeAddress(a);  // prescaler writes ($2d-$2f) are address-only
  }
}

void Ym2203::writeData(uint8_t value) {
  busy_left = 32u * pre;  // ymfm ym2203::write_data
  if (addr < Ay8910::REG_COUNT) {
    ssg_regs[addr] = value;
    return;
  }
  switch (addr) {
  case 0x24:
    a_val = (uint16_t)((a_val & 3) | (value << 2));
    break;
  case 0x25:
    a_val = (uint16_t)((a_val & 0x3fc) | (value & 3));
    break;
  case 0x26:
    b_val = value;
    break;
  case 0x27:
    writeMode(value);
    break;
  default:
    break;
  }
  if (fm) {
    fm->writeData(addr, value);
  }
}

void Ym2203::reset(void) {
  addr = 0;
  memset(ssg_regs, 0, sizeof(ssg_regs));
  a_val = 0;
  b_val = 0;
  pre = 6;
  mode = 0;
  status = 0;
  a_run = false;
  b_run = false;
  busy_left = 0;
  clocks = 0;
}

// reg 27: b0/b1 load, b2/b3 flag enable, b4/b5 flag reset (ymfm
// fm_engine::engine_mode_write). A running timer is not restarted.
// Timer B's x16 divider runs free: a start shortens its first count by
// the divider phase, 0-15 FM clocks (12 * prescaler chip cycles each).
void Ym2203::writeMode(uint8_t value) {
  mode = value;
  status &= (uint8_t)~((value >> 4) & 3);
  if ((value & 1) && !a_run) {
    a_run = true;
    a_left = periodA();
  } else if (!(value & 1)) {
    a_run = false;
  }
  if ((value & 2) && !b_run) {
    const uint32_t fm_clock = 12u * pre;
    b_run = true;
    b_left = periodB() - (clocks / fm_clock & 15) * fm_clock;
    clocks %= 16 * fm_clock;
  } else if (!(value & 2)) {
    b_run = false;
  }
}

void Ym2203::tick(uint32_t cycles) {
  clocks += cycles;
  busy_left = cycles < busy_left ? busy_left - cycles : 0;
  if (a_run) {
    uint32_t c = cycles;
    while (c >= a_left) {
      c -= a_left;
      if (mode & 4) {
        status |= 1;
      }
      a_left = periodA();
    }
    a_left -= c;
  }
  if (b_run) {
    uint32_t c = cycles;
    while (c >= b_left) {
      c -= b_left;
      if (mode & 8) {
        status |= 2;
      }
      b_left = periodB();
    }
    b_left -= c;
  }
}

void Ym2203::renderSsg(int16_t *out, int n) { ssg.render(ssg_regs, out, n); }
