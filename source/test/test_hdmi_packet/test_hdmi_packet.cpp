#include <unity.h>
#include <string.h>
#include "../../src/hal/rp2350/hdmi_packet.h"

using namespace hal::hdmi;

void setUp(void) {}
void tearDown(void) {}

// HDMI 1.4 table 5-4, 4 bit to 10 bit
static const uint16_t TERC4_SPEC[16] = {
  0x29c, 0x263, 0x2e4, 0x2e2, 0x171, 0x11e, 0x18e, 0x13c,
  0x2cc, 0x139, 0x19c, 0x2c6, 0x28e, 0x271, 0x163, 0x2c3,
};
static const uint16_t GUARD_BAND = 0x133;

// BCH(64,56) / BCH(32,24) parity, bit by bit, G(x) = 1 + x^6 + x^7 + x^8
static uint8_t bchRef(const uint8_t *data, int n) {
  uint8_t r = 0;
  for (int i = 0; i < n; i++) {
    for (int bit = 0; bit < 8; bit++) {
      const uint8_t b = ((data[i] >> bit) & 1) ^ (r & 1);
      r >>= 1;
      if (b) {
        r ^= 0x83;
      }
    }
  }
  return r;
}

static void assertParity(const Packet &p) {
  TEST_ASSERT_EQUAL_HEX8(bchRef(p.header, 3), p.header[3]);
  for (int k = 0; k < 4; k++) {
    TEST_ASSERT_EQUAL_HEX8(bchRef(p.sub[k], 7), p.sub[k][7]);
  }
}

// CEA-861 InfoFrame: header + length + 1 payload bytes (checksum first) sum to 0
static void assertChecksum(const Packet &p) {
  uint32_t sum = p.header[0] + p.header[1] + p.header[2];
  for (int i = 0; i <= p.header[2]; i++) {
    sum += p.sub[i / 7][i % 7];
  }
  TEST_ASSERT_EQUAL_HEX8(0, sum & 0xff);
}

static int decodeTerc4(uint32_t symbol) {
  for (int i = 0; i < 16; i++) {
    if (TERC4_SPEC[i] == symbol) {
      return i;
    }
  }
  TEST_FAIL_MESSAGE("not a TERC4 symbol");
  return -1;
}

static uint32_t lane(uint32_t word, int n) { return (word >> (10 * n)) & 0x3ff; }

static void test_bch_matches_reference(void) {
  Packet p;
  acrPacket(p, 6144, 25200);
  assertParity(p);
  aviInfoFrame(p);
  assertParity(p);
  audioInfoFrame(p);
  assertParity(p);
  nullPacket(p);
  assertParity(p);
  const int16_t s[4] = {0x1234, -2, 32767, -32768};
  samplePacket(p, s, 5);
  assertParity(p);
}

static void test_acr(void) {
  Packet p;
  acrPacket(p, 6144, 25200);
  TEST_ASSERT_EQUAL_HEX8(0x01, p.header[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, p.header[1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, p.header[2]);
  const uint8_t body[7] = {0x00, 0x00, 0x62, 0x70, 0x00, 0x18, 0x00};  // CTS 0x6270, N 0x1800
  for (int k = 0; k < 4; k++) {
    TEST_ASSERT_EQUAL_HEX8_ARRAY(body, p.sub[k], 7);
  }
}

static void test_infoframes(void) {
  Packet p;
  aviInfoFrame(p);
  TEST_ASSERT_EQUAL_HEX8(0x82, p.header[0]);
  TEST_ASSERT_EQUAL_HEX8(0x02, p.header[1]);
  TEST_ASSERT_EQUAL_HEX8(0x0d, p.header[2]);
  TEST_ASSERT_EQUAL_HEX8(1, p.sub[0][4]);  // VIC 1: 640x480 60 Hz
  assertChecksum(p);

  audioInfoFrame(p);
  TEST_ASSERT_EQUAL_HEX8(0x84, p.header[0]);
  TEST_ASSERT_EQUAL_HEX8(0x01, p.header[1]);
  TEST_ASSERT_EQUAL_HEX8(0x0a, p.header[2]);
  TEST_ASSERT_EQUAL_HEX8(0x11, p.sub[0][1]);  // L-PCM, 2 channels
  TEST_ASSERT_EQUAL_HEX8(0x0d, p.sub[0][2]);  // 48 kHz, 16 bit
  assertChecksum(p);
}

static void test_samples(void) {
  Packet p;
  const int16_t s[4] = {0x1234, -2, 0x0100, 0};
  TEST_ASSERT_EQUAL_UINT32(6, samplePacket(p, s, 2));
  TEST_ASSERT_EQUAL_HEX8(0x02, p.header[0]);
  TEST_ASSERT_EQUAL_HEX8(0x0f, p.header[1]);  // 4 samples present
  TEST_ASSERT_EQUAL_HEX8(0x00, p.header[2]);  // no block start

  // 24 bit L, then R, mono on both, 16 bit sample in the top bytes
  const uint8_t first[6] = {0x00, 0x34, 0x12, 0x00, 0x34, 0x12};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(first, p.sub[0], 6);
  const uint8_t second[6] = {0x00, 0xfe, 0xff, 0x00, 0xfe, 0xff};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(second, p.sub[1], 6);

  // byte 6: VL UL CL PL VR UR CR PR. Frame 2 carries channel status bit 2
  // (byte 0 = 0x04: consumer, L-PCM, copy permitted), parity is even over
  // the sample, V, U and C
  TEST_ASSERT_EQUAL_HEX8(0x44, p.sub[0][6]);  // 0x1234: 5 bits + C: P 0
  TEST_ASSERT_EQUAL_HEX8(0x88, p.sub[1][6]);  // frame 3, C 0, 0xfffe: 15 bits: P 1
}

static void test_samples_block_start(void) {
  Packet p;
  const int16_t s[4] = {0, 0, 0, 0};
  TEST_ASSERT_EQUAL_UINT32(2, samplePacket(p, s, 190));
  TEST_ASSERT_EQUAL_HEX8(0x40, p.header[2]);  // frame 0 is the third sample
}

static void assertIsland(const Packet &p, VSync v) {
  uint32_t w[ISLAND_WORDS];
  encodeIsland(w, p, v);

  // inside the hsync pulse (level 0); vsync level 0 during its pulse
  const int hv = v == VSync::PULSE ? 0 : 2;
  const uint32_t guard = TERC4_SPEC[0xc | hv] | (GUARD_BAND << 10) | (GUARD_BAND << 20);
  TEST_ASSERT_EQUAL_HEX32(guard, w[0]);
  TEST_ASSERT_EQUAL_HEX32(guard, w[1]);
  TEST_ASSERT_EQUAL_HEX32(guard, w[34]);
  TEST_ASSERT_EQUAL_HEX32(guard, w[35]);

  // lane 0: sync bits, header bit, bit 3 set after the first clock;
  // lane 1/2 bit k: bit 2t / 2t + 1 of subpacket k
  Packet got;
  memset(&got, 0, sizeof(got));
  for (int t = 0; t < 32; t++) {
    const int d0 = decodeTerc4(lane(w[t + 2], 0));
    const int d1 = decodeTerc4(lane(w[t + 2], 1));
    const int d2 = decodeTerc4(lane(w[t + 2], 2));
    TEST_ASSERT_EQUAL_INT(hv, d0 & 3);
    TEST_ASSERT_EQUAL_INT(t == 0 ? 0 : 8, d0 & 8);
    got.header[t / 8] |= ((d0 >> 2) & 1) << (t % 8);
    for (int k = 0; k < 4; k++) {
      const int even = 2 * t;
      const int odd = 2 * t + 1;
      got.sub[k][even / 8] |= ((d1 >> k) & 1) << (even % 8);
      got.sub[k][odd / 8] |= ((d2 >> k) & 1) << (odd % 8);
    }
  }
  TEST_ASSERT_EQUAL_HEX8_ARRAY(p.header, got.header, 4);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(p.sub, got.sub, 32);
}

static void test_island_round_trip(void) {
  Packet p;
  const int16_t s[4] = {0x1234, -2, 0x7fff, -0x8000};
  samplePacket(p, s, 0);
  assertIsland(p, VSync::OFF);
  assertIsland(p, VSync::PULSE);
  aviInfoFrame(p);
  assertIsland(p, VSync::OFF);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_bch_matches_reference);
  RUN_TEST(test_acr);
  RUN_TEST(test_infoframes);
  RUN_TEST(test_samples);
  RUN_TEST(test_samples_block_start);
  RUN_TEST(test_island_round_trip);
  return UNITY_END();
}
