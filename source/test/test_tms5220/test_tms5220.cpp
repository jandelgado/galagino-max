#include <string.h>
#include <unity.h>

#include "../../src/emulation/tms5220.cpp"

// Star Wars: 1.512 MHz CPU, 672 kHz chip
static const uint16_t CPS = 180;
static const uint32_t WS_CYCLES = CPS * 16 / 80;

enum : uint8_t { TS = 0x80, BL = 0x40, BE = 0x20 };
enum : uint8_t { SPEAK_EXTERNAL = 0x60 };

static SpeechRing ring;
static Tms5220 tms;
static uint32_t now;

void setUp(void) {
  now = 1000;
  ring.reset(8400, 8400);
  tms.reset(&ring, CPS, now);
}
void tearDown(void) {}

static void wait_ready(void) {
  for (int i = 0; i < 100000 && tms.readyq_r(now); i++) {
    now += 4;
  }
}

static void write_byte(uint8_t v) {
  tms.data_w(v);
  tms.wsq_w(false, now);
  wait_ready();
  tms.wsq_w(true, now);
}

static uint8_t read_status(void) {
  tms.rsq_w(false, now);
  wait_ready();
  const uint8_t s = tms.status_r(now);
  tms.rsq_w(true, now);
  return s;
}

// runs n samples, draining the ring; returns the largest |sample|
static int32_t run_samples(int n) {
  int32_t peak = 0;
  int16_t out[64];
  for (int done = 0; done < n; done += 64) {
    now += 64 * CPS;
    tms.update(now);
    ring.render(out, 64);
    for (int i = 0; i < 64; i++) {
      const int32_t a = out[i] < 0 ? -out[i] : out[i];
      peak = a > peak ? a : peak;
    }
  }
  return peak;
}

// LPC frame bits, first field MSB at byte bit 0 (MAME extract_bits)
struct BitWriter {
  uint8_t bytes[64];
  int pos;
  BitWriter() : pos(0) { memset(bytes, 0, sizeof(bytes)); }
  void put(uint32_t v, int bits) {
    for (int i = bits - 1; i >= 0; i--, pos++) {
      bytes[pos >> 3] |= ((v >> i) & 1) << (pos & 7);
    }
  }
  // energy, repeat 0, pitch, K1..K10
  void voiced(uint8_t energy, uint8_t pitch) {
    static const uint8_t KBITS[10] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};
    put(energy, 4);
    put(0, 1);
    put(pitch, 6);
    for (int k = 0; k < 10; k++) {
      put((1 << KBITS[k]) / 2 + 1, KBITS[k]);
    }
  }
  void stop() { put(0x0f, 4); }
  int size() const { return (pos + 7) >> 3; }
};

static void test_reset_status(void) {
  TEST_ASSERT_FALSE(tms.readyq_r(now));
  TEST_ASSERT_EQUAL_HEX8(BL | BE, read_status());
}

// /WS low: busy for 16 chip clocks, then the byte is taken
static void test_write_ready_timing(void) {
  tms.data_w(SPEAK_EXTERNAL);
  tms.wsq_w(false, now);
  TEST_ASSERT_TRUE(tms.readyq_r(now));
  TEST_ASSERT_TRUE(tms.readyq_r(now + WS_CYCLES - 1));
  TEST_ASSERT_FALSE(tms.readyq_r(now + WS_CYCLES));
}

// outside an /RS cycle the bus floats
static void test_status_outside_read(void) {
  TEST_ASSERT_EQUAL_HEX8(0xff, tms.status_r(now));
}

// BL clears with the 9th byte, which also starts talking
static void test_speak_external_fill(void) {
  write_byte(SPEAK_EXTERNAL);
  TEST_ASSERT_EQUAL_HEX8(BL | BE, read_status());
  for (int i = 0; i < 8; i++) {
    write_byte(0);
  }
  TEST_ASSERT_EQUAL_HEX8(BL, read_status());
  write_byte(0);
  TEST_ASSERT_EQUAL_HEX8(TS, read_status());
}

// a write to a full FIFO stays busy until the parser frees a byte
static void test_full_fifo_blocks(void) {
  write_byte(SPEAK_EXTERNAL);
  for (int i = 0; i < 16; i++) {
    write_byte(0);
  }
  tms.data_w(0);
  tms.wsq_w(false, now);
  now += 10 * WS_CYCLES;
  TEST_ASSERT_TRUE(tms.readyq_r(now));

  // silence frames take 4 bits, a frame is 200 samples
  const uint32_t start = now;
  wait_ready();
  TEST_ASSERT_TRUE(now - start < 800 * CPS);
  tms.wsq_w(true, now);
}

// voiced frames make sound, the stop frame ends talking
static void test_speech_then_stop(void) {
  BitWriter w;
  for (int f = 0; f < 4; f++) {
    w.voiced(12, 30);
  }
  w.stop();

  write_byte(SPEAK_EXTERNAL);
  for (int i = 0; i < w.size(); i++) {
    write_byte(w.bytes[i]);
  }
  TEST_ASSERT_EQUAL_HEX8(TS, read_status() & TS);

  TEST_ASSERT_TRUE(run_samples(6 * 200) > 256);
  TEST_ASSERT_EQUAL_HEX8(0, read_status() & TS);

  // idle chip outputs -1, once the ring has drained the speech
  run_samples(SpeechRing::SIZE * 2);
  TEST_ASSERT_TRUE(run_samples(4 * 200) <= 1);
}

// running out of data without a stop frame ends talking too
static void test_buffer_empty_ends_talk(void) {
  BitWriter w;
  w.voiced(12, 30);
  w.voiced(12, 30);

  write_byte(SPEAK_EXTERNAL);
  for (int i = 0; i < w.size(); i++) {
    write_byte(w.bytes[i]);
  }
  run_samples(6 * 200);
  TEST_ASSERT_EQUAL_HEX8(BL | BE, read_status());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_reset_status);
  RUN_TEST(test_write_ready_timing);
  RUN_TEST(test_status_outside_read);
  RUN_TEST(test_speak_external_fill);
  RUN_TEST(test_full_fifo_blocks);
  RUN_TEST(test_speech_then_stop);
  RUN_TEST(test_buffer_empty_ends_talk);
  return UNITY_END();
}
