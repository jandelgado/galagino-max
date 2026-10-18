#include "starwars_math.h"

// microcode strobes, PROM bits 15-8
enum : uint8_t {
  LAC = 0x01,       // load ACC from RAM, clears the lsb
  READ_ACC = 0x02,  // store ACC msb to RAM
  M_HALT = 0x04,
  INC_BIC = 0x08,
  CLEAR_ACC = 0x10,
  LDC = 0x20,       // load C, multiply-accumulate
  LDB = 0x40,
  LDA = 0x80,
};

enum : uint8_t { MW0 = 0, MW1 = 1, MW2 = 2, DVSRH = 4, DVSRL = 5, DVDDH = 6, DVDDL = 7 };

// MAME limit on instructions per run, ends runaway microcode
static const uint32_t MAX_STEPS = 100000;

void StarWarsMath::init(const uint16_t *microcode, uint8_t *math_ram) {
  prom = microcode;
  ram = math_ram;
}

void StarWarsMath::reset() {
  mpa = bic = 0;
  a = b = c = 0;
  acc = 0;
  dividend = divisor = dvd_shift = quotient = 0;
  prng = 1;
}

void StarWarsMath::run() {
  for (uint32_t step = 0; step < MAX_STEPS; step++) {
    const uint16_t ip = prom[mpa];
    const uint8_t strobe = ip >> 8;

    // RAM word address: block index counter or absolute
    const uint16_t ma = (ip & 0x80) ? (ip & 0x7f) : ((ip & 3) | ((bic & 0x1ff) << 2));
    uint8_t *word = ram + (ma << 1);
    const uint16_t ramword = (word[0] << 8) | word[1];

    if (strobe & CLEAR_ACC) {
      acc = 0;
    }
    if (strobe & LAC) {
      acc = (uint32_t)ramword << 16;
    }
    if (strobe & READ_ACC) {
      word[0] = acc >> 24;
      word[1] = acc >> 16;
    }
    if (strobe & INC_BIC) {
      bic = (bic + 1) & 0x1ff;
    }
    if (strobe & LDC) {
      // ACC += (A - B) * C, wrapping like the 32 bit ring; afterwards A
      // and B only hold their sign
      c = ramword;
      const uint32_t diff = (uint32_t)((int32_t)(int16_t)a - (int16_t)b);
      acc += (diff * (uint32_t)(int32_t)(int16_t)c) << 2;
      a = (a & 0x8000) ? 0xffff : 0;
      b = (b & 0x8000) ? 0xffff : 0;
    }
    if (strobe & LDB) {
      b = ramword;
    }
    if (strobe & LDA) {
      a = ramword;
    }

    // the counter covers the low 8 bits; each page of 256 wraps
    mpa = (mpa & 0x300) | ((mpa + 1) & 0xff);
    if (strobe & M_HALT) {
      return;
    }
  }
}

// restoring division as on the schematics, with the hardware's wrong
// results for divisor < 2 * dividend or divisor > 0x8000
void StarWarsMath::divide() {
  for (uint8_t i = 1; i < 16; i++) {
    quotient <<= 1;
    const uint32_t diff = (uint32_t)dvd_shift + (divisor ^ 0xffff) + 1;
    if (diff & 0x10000) {
      quotient |= 1;
      dvd_shift = diff << 1;
    } else {
      dvd_shift <<= 1;
    }
  }
}

void StarWarsMath::write(uint8_t offset, uint8_t v) {
  switch (offset) {
    case MW0:
      mpa = v << 2;
      run();
      break;
    case MW1:
      bic = (bic & 0xff) | ((v & 1) << 8);
      break;
    case MW2:
      bic = (bic & 0x100) | v;
      break;
    case DVSRH:
      divisor = (divisor & 0xff) | (v << 8);
      dvd_shift = dividend;
      quotient = 0;
      break;
    case DVSRL: // the 6809 writes high byte first, so low starts the divide
      divisor = (divisor & 0xff00) | v;
      divide();
      break;
    case DVDDH:
      dividend = (dividend & 0xff) | (v << 8);
      break;
    case DVDDL:
      dividend = (dividend & 0xff00) | v;
      break;
  }
}

// MAME uses its own PRNG too; xorshift32
uint8_t StarWarsMath::random() {
  prng ^= prng << 13;
  prng ^= prng >> 17;
  prng ^= prng << 5;
  return prng >> 8;
}
