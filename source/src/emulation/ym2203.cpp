#include <string.h>
#include "ym2203.h"
#include "ym2203fm.h"

Ym2203::Ym2203(uint8_t ssg_inc, uint8_t ssg_gain, Ym2203Fm *fm)
    : fm(fm), ssg(ssg_inc, ssg_gain) {}

void Ym2203::writeAddress(uint8_t a) {
  addr = a;
  if (fm) {
    fm->writeAddress(a);  // prescaler writes ($2d-$2f) are address-only
  }
}

void Ym2203::writeData(uint8_t value) {
  if (addr < Ay8910::REG_COUNT) {
    ssg_regs[addr] = value;
    return;
  }
  if (fm) {
    fm->writeData(addr, value);
  }
}

void Ym2203::reset(void) {
  addr = 0;
  memset(ssg_regs, 0, sizeof(ssg_regs));
}

void Ym2203::renderSsg(int16_t *out, int n) { ssg.render(ssg_regs, out, n); }
