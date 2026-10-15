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

// channel 1, algorithm 7, only operator 1 audible: a sine at 440 Hz (fnum
// 0x568, block 4 at 41666 Hz); AR 31, DR/SR 0, SL 0: holds while keyed
static const uint8_t SINE_PATCH[][2] = {
  {0xb0, 0x07}, {0x30, 0x01}, {0x34, 0x01}, {0x38, 0x01}, {0x3c, 0x01},
  {0x40, 0x00}, {0x44, 0x7f}, {0x48, 0x7f}, {0x4c, 0x7f},
  {0x50, 0x1f}, {0x54, 0x1f}, {0x58, 0x1f}, {0x5c, 0x1f},
  {0xa4, 0x25}, {0xa0, 0x68}};
static const uint8_t KEY_OP1_CH1 = 0x10;
static const uint8_t KEY_OFF_CH1 = 0x00;
static const uint8_t KEY_REG = 0x28;

static void loadSine(Ym2203Fm &y, bool via_ring) {
  for (const auto &w : SINE_PATCH) {
    if (via_ring) {
      y.writeData(w[0], w[1]);
    } else {
      y.applyWrite(w[0], w[1]);
    }
  }
}

static int32_t peak(const int32_t *s, int n) {
  int32_t p = 0;
  for (int i = 0; i < n; i++) {
    const int32_t a = s[i] < 0 ? -s[i] : s[i];
    p = a > p ? a : p;
  }
  return p;
}

static int32_t out[24000];

// render at the FM rate equals clock() sample for sample
void test_render_native_equals_clock(void) {
  static Ym2203Fm a, b;
  a.reset();
  b.reset();
  a.writeAddress(0x2d);
  a.writeAddress(0x2e);
  b.applyAddress(0x2d);
  b.applyAddress(0x2e);
  loadSine(a, true);
  loadSine(b, false);
  a.writeData(KEY_REG, KEY_OP1_CH1);
  b.applyWrite(KEY_REG, KEY_OP1_CH1);

  memset(out, 0, sizeof(out));
  a.render(out, 256, 41666);
  for (int i = 0; i < 256; i++) {
    TEST_ASSERT_EQUAL_INT32(b.clock(), out[i]);
  }
}

// at 24 kHz the note keeps its pitch: 440 rising zero crossings per second
void test_render_24k_pitch(void) {
  ym.reset();
  ym.writeAddress(0x2d);
  ym.writeAddress(0x2e);
  loadSine(ym, true);
  ym.writeData(KEY_REG, KEY_OP1_CH1);

  memset(out, 0, sizeof(out));
  ym.render(out, 24000, 24000);
  int crossings = 0;
  for (int i = 1; i < 24000; i++) {
    if (out[i - 1] < 0 && out[i] >= 0) {
      crossings++;
    }
  }
  TEST_ASSERT_INT_WITHIN(2, 440, crossings);
}

// a note decays to silence (DR 31, SL 15); key off + key on queued for the
// same buffer must retrigger it, key on alone must not
static void decayedNote(Ym2203Fm &y) {
  y.reset();
  loadSine(y, true);
  y.writeData(0x60, 0x1f); // DR 31
  y.writeData(0x80, 0xf0); // SL 15
  y.writeData(KEY_REG, KEY_OP1_CH1);
  memset(out, 0, sizeof(out));
  y.render(out, 4000, 24000);
  TEST_ASSERT_EQUAL_INT32(0, peak(out + 3936, 64));
}

void test_ring_keeps_keyoff_keyon_order(void) {
  decayedNote(ym);
  ym.writeData(KEY_REG, KEY_OFF_CH1);
  ym.writeData(KEY_REG, KEY_OP1_CH1);
  memset(out, 0, sizeof(out));
  ym.render(out, 64, 24000);
  TEST_ASSERT_GREATER_THAN_INT32(1000, peak(out, 64));
}

void test_keyon_alone_does_not_retrigger(void) {
  decayedNote(ym);
  ym.writeData(KEY_REG, KEY_OP1_CH1);
  memset(out, 0, sizeof(out));
  ym.render(out, 64, 24000);
  TEST_ASSERT_EQUAL_INT32(0, peak(out, 64));
}

// a full ring drops the newest writes and counts them
void test_ring_overflow(void) {
  ym.reset();
  for (int i = 0; i < 300; i++) {
    ym.writeData(0x40, (uint8_t)i);
  }
  TEST_ASSERT_EQUAL_UINT16(300 - (Ym2203Fm::RING_SIZE - 1), ym.dropped());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_prescale_rate);
  RUN_TEST(test_chip1_matches_ymfm);
  RUN_TEST(test_chip2_matches_ymfm);
  RUN_TEST(test_render_native_equals_clock);
  RUN_TEST(test_render_24k_pitch);
  RUN_TEST(test_ring_keeps_keyoff_keyon_order);
  RUN_TEST(test_keyon_alone_does_not_retrigger);
  RUN_TEST(test_ring_overflow);
  return UNITY_END();
}
