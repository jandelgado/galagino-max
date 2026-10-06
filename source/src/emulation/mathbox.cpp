#include "mathbox.h"
#include <string.h>

// Line by line port of MAME mathbox_device::go_w. All registers are
// int16_t; the casts keep MAME's int16 truncation explicit.
void Mathbox::reset() {
  result = 0;
  memset(reg, 0, sizeof(reg));
}

// register load: low byte (even commands) or high byte of reg[r]
static int16_t load_lo(int16_t r, uint8_t data) { return (int16_t)((r & 0xff00) | data); }
static int16_t load_hi(int16_t r, uint8_t data) { return (int16_t)((r & 0x00ff) | (data << 8)); }

void Mathbox::go(uint8_t offset, uint8_t data) {
  int32_t mb_temp; // temp 32-bit multiply results
  int16_t mb_q;    // temp used in division
  int msb;

  switch (offset) {
    case 0x00: result = reg[0x0] = load_lo(reg[0x0], data); break;
    case 0x01: result = reg[0x0] = load_hi(reg[0x0], data); break;
    case 0x02: result = reg[0x1] = load_lo(reg[0x1], data); break;
    case 0x03: result = reg[0x1] = load_hi(reg[0x1], data); break;
    case 0x04: result = reg[0x2] = load_lo(reg[0x2], data); break;
    case 0x05: result = reg[0x2] = load_hi(reg[0x2], data); break;
    case 0x06: result = reg[0x3] = load_lo(reg[0x3], data); break;
    case 0x07: result = reg[0x3] = load_hi(reg[0x3], data); break;
    case 0x08: result = reg[0x4] = load_lo(reg[0x4], data); break;
    case 0x09: result = reg[0x4] = load_hi(reg[0x4], data); break;

    // no function loads the high part of reg 5 without computing
    case 0x0a: result = reg[0x5] = load_lo(reg[0x5], data); break;

    // no function loads the high part of reg 6
    case 0x0c: result = reg[0x6] = data; break;

    case 0x15: result = reg[0x7] = load_lo(reg[0x7], data); break;
    case 0x16: result = reg[0x7] = load_hi(reg[0x7], data); break;

    case 0x1a: result = reg[0x8] = load_lo(reg[0x8], data); break;
    case 0x1b: result = reg[0x8] = load_hi(reg[0x8], data); break;

    case 0x0d: result = reg[0xa] = load_lo(reg[0xa], data); break;
    case 0x0e: result = reg[0xa] = load_hi(reg[0xa], data); break;
    case 0x0f: result = reg[0xb] = load_lo(reg[0xb], data); break;
    case 0x10: result = reg[0xb] = load_hi(reg[0xb], data); break;

    case 0x17: result = reg[0x7]; break;
    case 0x19: result = reg[0x8]; break;
    case 0x18: result = reg[0x9]; break;

    case 0x0b:
      reg[0x5] = load_hi(reg[0x5], data);

      reg[0xf] = (int16_t)0xffff;
      reg[0x4] = (int16_t)(reg[0x4] - reg[0x2]);
      reg[0x5] = (int16_t)(reg[0x5] - reg[0x3]);

    step_048:
      mb_temp = (int32_t)reg[0x0] * (int32_t)reg[0x4];
      reg[0xc] = (int16_t)(mb_temp >> 16);
      reg[0xe] = (int16_t)(mb_temp & 0xffff);

      mb_temp = (int32_t)-reg[0x1] * (int32_t)reg[0x5];
      reg[0x7] = (int16_t)(mb_temp >> 16);
      mb_q = (int16_t)(mb_temp & 0xffff);

      reg[0x7] = (int16_t)(reg[0x7] + reg[0xc]);

      // rounding
      reg[0xe] = (int16_t)((reg[0xe] >> 1) & 0x7fff);
      reg[0xc] = (int16_t)((mb_q >> 1) & 0x7fff);
      mb_q = (int16_t)(reg[0xc] + reg[0xe]);
      if (mb_q < 0) {
        reg[0x7]++;
      }

      result = reg[0x7];

      if (reg[0xf] < 0) {
        break;
      }

      reg[0x7] = (int16_t)(reg[0x7] + reg[0x2]);
      // fall through into command 0x12

    case 0x12:
      mb_temp = (int32_t)reg[0x1] * (int32_t)reg[0x4];
      reg[0xc] = (int16_t)(mb_temp >> 16);
      reg[0x9] = (int16_t)(mb_temp & 0xffff);

      mb_temp = (int32_t)reg[0x0] * (int32_t)reg[0x5];
      reg[0x8] = (int16_t)(mb_temp >> 16);
      mb_q = (int16_t)(mb_temp & 0xffff);

      reg[0x8] = (int16_t)(reg[0x8] + reg[0xc]);

      // rounding
      reg[0x9] = (int16_t)((reg[0x9] >> 1) & 0x7fff);
      reg[0xc] = (int16_t)((mb_q >> 1) & 0x7fff);
      reg[0x9] = (int16_t)(reg[0x9] + reg[0xc]);
      if (reg[0x9] < 0) {
        reg[0x8]++;
      }
      reg[0x9] = (int16_t)(reg[0x9] << 1);

      result = reg[0x8];

      if (reg[0xf] < 0) {
        break;
      }

      reg[0x8] = (int16_t)(reg[0x8] + reg[0x3]);

      reg[0x9] = (int16_t)(reg[0x9] & 0xff00);
      // fall through into command 0x13

    case 0x13:
      reg[0xc] = reg[0x9];
      mb_q = reg[0x8];
      goto step_0bf;

    case 0x14:
      reg[0xc] = reg[0xa];
      mb_q = reg[0xb];

    step_0bf:
      reg[0xe] = (int16_t)(reg[0x7] ^ mb_q); // save sign of result
      reg[0xd] = mb_q;
      if (mb_q >= 0) {
        mb_q = reg[0xc];
      } else {
        reg[0xd] = (int16_t)(-mb_q - 1);
        mb_q = (int16_t)(-reg[0xc] - 1);
        if ((mb_q < 0) && ((mb_q + 1) < 0)) {
          reg[0xd]++;
        }
        mb_q++;
      }

      // step 0c9: reg c = abs(reg 7)
      if (reg[0x7] >= 0) {
        reg[0xc] = reg[0x7];
      } else {
        reg[0xc] = (int16_t)-reg[0x7];
      }

      reg[0xf] = reg[0x6]; // step counter

      do {
        reg[0xd] = (int16_t)(reg[0xd] - reg[0xc]);
        msb = (mb_q & 0x8000) != 0;
        mb_q = (int16_t)(mb_q << 1);
        if (reg[0xd] >= 0) {
          mb_q++;
        } else {
          reg[0xd] = (int16_t)(reg[0xd] + reg[0xc]);
        }
        reg[0xd] = (int16_t)(reg[0xd] << 1);
        reg[0xd] = (int16_t)(reg[0xd] + msb);
      } while (--reg[0xf] >= 0);

      if (reg[0xe] >= 0) {
        result = mb_q;
      } else {
        result = (int16_t)-mb_q;
      }
      break;

    case 0x11:
      reg[0x5] = load_hi(reg[0x5], data);
      reg[0xf] = 0x0000; // do everything in one step
      goto step_048;

    case 0x1c:
      // window test?
      reg[0x5] = load_hi(reg[0x5], data);
      do {
        reg[0xe] = (int16_t)((reg[0x4] + reg[0x7]) >> 1);
        reg[0xf] = (int16_t)((reg[0x5] + reg[0x8]) >> 1);
        if ((reg[0xb] < reg[0xe]) && (reg[0xf] < reg[0xe]) && ((reg[0xe] + reg[0xf]) >= 0)) {
          reg[0x7] = reg[0xe];
          reg[0x8] = reg[0xf];
        } else {
          reg[0x4] = reg[0xe];
          reg[0x5] = reg[0xf];
        }
      } while (--reg[0x6] >= 0);

      result = reg[0x8];
      break;

    case 0x1d:
      reg[0x3] = load_hi(reg[0x3], data);

      reg[0x2] = (int16_t)(reg[0x2] - reg[0x0]);
      if (reg[0x2] < 0) {
        reg[0x2] = (int16_t)-reg[0x2];
      }

      reg[0x3] = (int16_t)(reg[0x3] - reg[0x1]);
      if (reg[0x3] < 0) {
        reg[0x3] = (int16_t)-reg[0x3];
      }
      // fall through into command 0x1e

    case 0x1e:
      // result = max(reg 2, reg 3) + 3/8 * min(reg 2, reg 3)
      if (reg[0x3] >= reg[0x2]) {
        reg[0xc] = reg[0x2];
        reg[0xd] = reg[0x3];
      } else {
        reg[0xd] = reg[0x2];
        reg[0xc] = reg[0x3];
      }
      reg[0xc] = (int16_t)(reg[0xc] >> 2);
      reg[0xd] = (int16_t)(reg[0xd] + reg[0xc]);
      reg[0xc] = (int16_t)(reg[0xc] >> 1);
      result = reg[0xd] = (int16_t)(reg[0xc] + reg[0xd]);
      break;

    case 0x1f:
      // MAME: unknown (self test / signature analysis?), only logged
      break;
  }
}
