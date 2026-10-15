#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "../../src/emulation/ym2203fm.cpp"
#include "ym_trace_1943.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

static Ym2203Fm ym;

// replays the MAME 1943b trace tick by tick: every FM sample must equal
// ymfm's (head samples one by one, then the CRC of the whole stream)
static void replay(uint8_t chip) {
  ym.reset();
  const size_t n = sizeof(YM_TRACE) / sizeof(YM_TRACE[0]);
  size_t w = 0;
  uint32_t crc = 0;
  for (uint32_t t = 0; t < YM_TRACE_TICKS[chip]; t++) {
    for (; w < n && YM_TRACE[w].tick <= t; w++) {
      const YmTraceWrite &e = YM_TRACE[w];
      if (e.chip != chip) {
        continue;
      }
      if (e.is_addr) {
        ym.applyAddress(e.reg);
      } else {
        ym.applyWrite(e.reg, e.value);
      }
    }
    const int16_t s = ym.clock();
    const uint32_t h = t - YM_REF_HEAD_START[chip];
    if (t >= YM_REF_HEAD_START[chip] && h < 4096 && s != YM_REF_HEAD[chip][h]) {
      char msg[96];
      snprintf(msg, sizeof(msg), "chip %u head sample %u: got %d, ymfm %d", chip, (unsigned)h, s,
               YM_REF_HEAD[chip][h]);
      TEST_FAIL_MESSAGE(msg);
    }
    const uint8_t le[2] = {(uint8_t)s, (uint8_t)((uint16_t)s >> 8)};
    crc = crc32_update(crc, le, 2);
  }
  TEST_ASSERT_EQUAL_HEX32(YM_REF_CRC[chip], crc);
}

void test_chip1_matches_ymfm(void) { replay(0); }
void test_chip2_matches_ymfm(void) { replay(1); }

// $2D/$2E/$2F address writes select the prescaler; $2E only from 6
void test_prescale_rate(void) {
  ym.reset();
  TEST_ASSERT_EQUAL_UINT32(20833, ym.fmRate());
  ym.applyAddress(0x2e);
  TEST_ASSERT_EQUAL_UINT32(41666, ym.fmRate());
  ym.applyAddress(0x2f);
  TEST_ASSERT_EQUAL_UINT32(62500, ym.fmRate());
  ym.applyAddress(0x2e); // only from prescale 6
  TEST_ASSERT_EQUAL_UINT32(62500, ym.fmRate());
  ym.applyAddress(0x2d);
  TEST_ASSERT_EQUAL_UINT32(20833, ym.fmRate());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_prescale_rate);
  RUN_TEST(test_chip1_matches_ymfm);
  RUN_TEST(test_chip2_matches_ymfm);
  return UNITY_END();
}
