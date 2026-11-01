#ifndef HAL_RP2350_HDMI_PACKET_H
#define HAL_RP2350_HDMI_PACKET_H

// HDMI data island packets for the DVI scanout: audio samples, Audio Clock
// Regeneration, InfoFrames, and their TERC4 coding into HSTX raw words (one
// 30-bit word per pixel clock, 10 bits per lane). The scanout IRQ encodes a
// sample packet every 2-3 lines, so code and tables live in RAM. No SDK
// dependencies, so host tests cover them. Follows pico-galagino's
// pico_shared/drivers/pico_hdmi/hstx_packet.c (GPLv3).

#include <stdint.h>
#include "../hal_attr.h"

namespace hal {
namespace hdmi {

// Header: 3 bytes + BCH parity. Subpackets: 7 bytes + BCH parity each.
struct Packet {
  uint8_t header[4];
  uint8_t sub[4][8];
};

// Guard band, 32 packet clocks, guard band
static const uint32_t ISLAND_WORDS = 36;

// Islands sit in the hsync pulse; vsync is either in its pulse or not.
enum class VSync : uint8_t { OFF, PULSE };

// IEC 60958 channel status block length in frames
static const uint32_t CS_FRAMES = 192;

static const uint16_t DATA_GUARD_BAND = 0x133;

// BCH parity byte by byte, G(x) = 1 + x^6 + x^7 + x^8
static const uint8_t BCH[256] HAL_FAST_DATA = {
  0x00, 0xd9, 0xb5, 0x6c, 0x6d, 0xb4, 0xd8, 0x01, 0xda, 0x03, 0x6f, 0xb6, 0xb7, 0x6e, 0x02, 0xdb,
  0xb3, 0x6a, 0x06, 0xdf, 0xde, 0x07, 0x6b, 0xb2, 0x69, 0xb0, 0xdc, 0x05, 0x04, 0xdd, 0xb1, 0x68,
  0x61, 0xb8, 0xd4, 0x0d, 0x0c, 0xd5, 0xb9, 0x60, 0xbb, 0x62, 0x0e, 0xd7, 0xd6, 0x0f, 0x63, 0xba,
  0xd2, 0x0b, 0x67, 0xbe, 0xbf, 0x66, 0x0a, 0xd3, 0x08, 0xd1, 0xbd, 0x64, 0x65, 0xbc, 0xd0, 0x09,
  0xc2, 0x1b, 0x77, 0xae, 0xaf, 0x76, 0x1a, 0xc3, 0x18, 0xc1, 0xad, 0x74, 0x75, 0xac, 0xc0, 0x19,
  0x71, 0xa8, 0xc4, 0x1d, 0x1c, 0xc5, 0xa9, 0x70, 0xab, 0x72, 0x1e, 0xc7, 0xc6, 0x1f, 0x73, 0xaa,
  0xa3, 0x7a, 0x16, 0xcf, 0xce, 0x17, 0x7b, 0xa2, 0x79, 0xa0, 0xcc, 0x15, 0x14, 0xcd, 0xa1, 0x78,
  0x10, 0xc9, 0xa5, 0x7c, 0x7d, 0xa4, 0xc8, 0x11, 0xca, 0x13, 0x7f, 0xa6, 0xa7, 0x7e, 0x12, 0xcb,
  0x83, 0x5a, 0x36, 0xef, 0xee, 0x37, 0x5b, 0x82, 0x59, 0x80, 0xec, 0x35, 0x34, 0xed, 0x81, 0x58,
  0x30, 0xe9, 0x85, 0x5c, 0x5d, 0x84, 0xe8, 0x31, 0xea, 0x33, 0x5f, 0x86, 0x87, 0x5e, 0x32, 0xeb,
  0xe2, 0x3b, 0x57, 0x8e, 0x8f, 0x56, 0x3a, 0xe3, 0x38, 0xe1, 0x8d, 0x54, 0x55, 0x8c, 0xe0, 0x39,
  0x51, 0x88, 0xe4, 0x3d, 0x3c, 0xe5, 0x89, 0x50, 0x8b, 0x52, 0x3e, 0xe7, 0xe6, 0x3f, 0x53, 0x8a,
  0x41, 0x98, 0xf4, 0x2d, 0x2c, 0xf5, 0x99, 0x40, 0x9b, 0x42, 0x2e, 0xf7, 0xf6, 0x2f, 0x43, 0x9a,
  0xf2, 0x2b, 0x47, 0x9e, 0x9f, 0x46, 0x2a, 0xf3, 0x28, 0xf1, 0x9d, 0x44, 0x45, 0x9c, 0xf0, 0x29,
  0x20, 0xf9, 0x95, 0x4c, 0x4d, 0x94, 0xf8, 0x21, 0xfa, 0x23, 0x4f, 0x96, 0x97, 0x4e, 0x22, 0xfb,
  0x93, 0x4a, 0x26, 0xff, 0xfe, 0x27, 0x4b, 0x92, 0x49, 0x90, 0xfc, 0x25, 0x24, 0xfd, 0x91, 0x48,
};

static inline HAL_FAST_CODE uint8_t bch(const uint8_t *data, uint32_t n) {
  uint8_t v = 0;
  for (uint32_t i = 0; i < n; i++) {
    v = BCH[data[i] ^ v];
  }
  return v;
}

// Compile-time tables of the sample packet path, which runs in the scanout
// IRQ. C++11 constexpr: one return statement per function.
namespace table {

// BCH state after feeding bits i .. 7 of x (LSB first) into state r
constexpr uint8_t bchBits(uint8_t r, uint32_t x, int i) {
  return i == 8 ? r
                : bchBits((((x >> i) ^ r) & 1) ? (uint8_t)((r >> 1) ^ 0x83) : (uint8_t)(r >> 1), x, i + 1);
}
constexpr uint8_t bchZeros(uint8_t r, int n) { return n == 0 ? r : bchZeros(bchBits(r, 0, 0), n - 1); }

// BCH parity is linear: a subpacket's parity is the XOR of each byte's
// parity alone; x at position `pos` of the 7 data bytes
constexpr uint8_t bchAt(int pos, uint32_t x) { return bchZeros(bchBits(0, x, 0), 6 - pos); }
// a sample byte sits in both channels: low byte at 1 and 4, high at 2 and 5
constexpr uint8_t bchLo(uint32_t x) { return (uint8_t)(bchAt(1, x) ^ bchAt(4, x)); }
constexpr uint8_t bchHi(uint32_t x) { return (uint8_t)(bchAt(2, x) ^ bchAt(5, x)); }

// TERC4: 4 bit to 10 bit symbol (HDMI 1.4 table 5-4)
constexpr uint16_t TERC4[16] = {
  0x29c, 0x263, 0x2e4, 0x2e2, 0x171, 0x11e, 0x18e, 0x13c,
  0x2cc, 0x139, 0x19c, 0x2c6, 0x28e, 0x271, 0x163, 0x2c3,
};
constexpr uint16_t terc4(uint32_t i) { return TERC4[i]; }
// bits 0, 2, 4, 6 / 1, 3, 5, 7 of i as a nibble
constexpr uint32_t evens(uint32_t i) { return (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4) | ((i >> 3) & 8); }
// lanes 1 and 2 of an HSTX word from interleaved bit pairs (see encodeIsland)
constexpr uint32_t lanes(uint32_t i) { return ((uint32_t)TERC4[evens(i)] << 10) | ((uint32_t)TERC4[evens(i >> 1)] << 20); }

} // namespace table

#define HDMI_T4(f, i) table::f(i), table::f(i + 1), table::f(i + 2), table::f(i + 3)
#define HDMI_T16(f, i) HDMI_T4(f, i), HDMI_T4(f, i + 4), HDMI_T4(f, i + 8), HDMI_T4(f, i + 12)
#define HDMI_T64(f, i) HDMI_T16(f, i), HDMI_T16(f, i + 16), HDMI_T16(f, i + 32), HDMI_T16(f, i + 48)
#define HDMI_T256(f) HDMI_T64(f, 0), HDMI_T64(f, 64), HDMI_T64(f, 128), HDMI_T64(f, 192)
static const uint16_t TERC4[16] HAL_FAST_DATA = {HDMI_T16(terc4, 0)};
static const uint8_t BCH_LO[256] HAL_FAST_DATA = {HDMI_T256(bchLo)};
static const uint8_t BCH_HI[256] HAL_FAST_DATA = {HDMI_T256(bchHi)};
// byte 6 of a sample subpacket by C | P << 1: 0x00, 0x44, 0x88, 0xcc
static const uint8_t BCH_STATUS[4] HAL_FAST_DATA = {table::bchAt(6, 0x00), table::bchAt(6, 0x44),
                                                    table::bchAt(6, 0x88), table::bchAt(6, 0xcc)};
static const uint32_t TERC4_LANES[256] HAL_FAST_DATA = {HDMI_T256(lanes)};
#undef HDMI_T256
#undef HDMI_T64
#undef HDMI_T16
#undef HDMI_T4

static inline HAL_FAST_CODE uint32_t parity8(uint32_t v) {
  v ^= v >> 4;
  v ^= v >> 2;
  v ^= v >> 1;
  return v & 1;
}

// the loop, not memset: memset lives in flash
__attribute__((optimize("no-tree-loop-distribute-patterns")))
static inline HAL_FAST_CODE void clear(Packet &p) {
  uint8_t *bytes = (uint8_t *)&p;
  for (uint32_t i = 0; i < sizeof(Packet); i++) {
    bytes[i] = 0;
  }
}

static inline HAL_FAST_CODE void setParity(Packet &p) {
  p.header[3] = bch(p.header, 3);
  for (int k = 0; k < 4; k++) {
    p.sub[k][7] = bch(p.sub[k], 7);
  }
}

// InfoFrame payload starts with its checksum: all bytes sum to 0
static inline void setChecksum(Packet &p) {
  uint32_t sum = p.header[0] + p.header[1] + p.header[2];
  for (uint32_t i = 1; i <= p.header[2]; i++) {
    sum += p.sub[i / 7][i % 7];
  }
  p.sub[0][0] = (uint8_t)(0x100 - (sum & 0xff));
}

static inline void nullPacket(Packet &p) {
  clear(p);
  setParity(p);
}

// Audio Clock Regeneration: the sink derives 128 * fs = f_TMDS * N / CTS
static inline void acrPacket(Packet &p, uint32_t n, uint32_t cts) {
  clear(p);
  p.header[0] = 0x01;
  for (int k = 0; k < 4; k++) {
    p.sub[k][1] = (cts >> 16) & 0x0f;
    p.sub[k][2] = (cts >> 8) & 0xff;
    p.sub[k][3] = cts & 0xff;
    p.sub[k][4] = (n >> 16) & 0x0f;
    p.sub[k][5] = (n >> 8) & 0xff;
    p.sub[k][6] = n & 0xff;
  }
  setParity(p);
}

// AVI InfoFrame v2, VIC 1 (640x480 60 Hz), 4:3; marks the link as HDMI
static inline void aviInfoFrame(Packet &p) {
  clear(p);
  p.header[0] = 0x82;
  p.header[1] = 0x02;
  p.header[2] = 0x0d;
  p.sub[0][1] = 0x10;  // RGB, active format valid
  p.sub[0][2] = 0x18;  // 4:3, active format same as picture
  p.sub[0][4] = 1;
  setChecksum(p);
  setParity(p);
}

// Audio InfoFrame: 2 channels. HDMI 1.4 8.2.2: coding type, sample rate
// and size stay 0, the sink reads them from the stream (channel status).
static inline void audioInfoFrame(Packet &p) {
  clear(p);
  p.header[0] = 0x84;
  p.header[1] = 0x01;
  p.header[2] = 0x0a;
  p.sub[0][1] = 0x01;
  setChecksum(p);
  setParity(p);
}

// IEC 60958 channel status, bit i in frame i: consumer, L-PCM, copy
// permitted, 48 kHz; the rest is 0. Strict sinks mute without it.
static inline HAL_FAST_CODE uint32_t channelStatus(uint32_t frame) {
  static const uint8_t CS[4] HAL_FAST_DATA = {0x04, 0x00, 0x00, 0x02};
  return frame < 32 ? (CS[frame / 8] >> (frame % 8)) & 1 : 0;
}

// Four mono samples to both channels, frames `frame` .. `frame` + 3 of the
// channel status block. Returns the next frame.
static inline HAL_FAST_CODE uint32_t samplePacket(Packet &p, const int16_t *samples, uint32_t frame) {
  uint8_t blockStart = 0;
  for (int k = 0; k < 4; k++) {
    if (frame == 0) {
      blockStart |= 0x10 << k;
    }

    // 24 bit samples, the 16 bit sample in the top bytes
    uint8_t *d = p.sub[k];
    const uint8_t lo = (uint8_t)samples[k];
    const uint8_t hi = (uint8_t)((uint16_t)samples[k] >> 8);
    d[0] = 0;
    d[1] = lo;
    d[2] = hi;
    d[3] = 0;
    d[4] = lo;
    d[5] = hi;

    // byte 6: VL UL CL PL VR UR CR PR, P even over sample, V, U, C
    const uint32_t c = channelStatus(frame);
    const uint32_t parity = parity8(lo ^ hi) ^ c;
    d[6] = (uint8_t)((c << 2) | (parity << 3) | (c << 6) | (parity << 7));
    d[7] = BCH_LO[lo] ^ BCH_HI[hi] ^ BCH_STATUS[c | (parity << 1)];
    frame = frame + 1 == CS_FRAMES ? 0 : frame + 1;
  }
  p.header[0] = 0x02;
  p.header[1] = 0x0f;
  p.header[2] = blockStart;
  p.header[3] = bch(p.header, 3);
  return frame;
}

static inline HAL_FAST_CODE uint32_t word(uint32_t lane0, uint32_t lane1, uint32_t lane2) {
  return lane0 | (lane1 << 10) | (lane2 << 20);
}

// TERC4 coded island. Lane 0: hsync, vsync, header bit, "not first clock";
// lane 1 / 2 bit k: bit 2t / 2t + 1 of subpacket k at clock t.
static inline HAL_FAST_CODE void encodeIsland(uint32_t *out, const Packet &p, VSync v) {
  const uint32_t hv = v == VSync::PULSE ? 0 : 2;  // sync levels, both active low
  const uint32_t guard = word(TERC4[0xc | hv], DATA_GUARD_BAND, DATA_GUARD_BAND);
  out[0] = guard;
  out[1] = guard;
  out[34] = guard;
  out[35] = guard;

  for (uint32_t i = 0; i < 8; i++) {
    // byte i of the subpackets, subpacket k in byte k of v
    const uint32_t v = p.sub[0][i] | (p.sub[1][i] << 8) | (p.sub[2][i] << 16) | ((uint32_t)p.sub[3][i] << 24);
    const uint8_t h = p.header[i / 2];
    for (uint32_t j = 0; j < 4; j++) {
      // bits 2j, 2j + 1 of each byte side by side: the multiply moves the
      // pairs at 0, 8, 16, 24 to 24, 26, 28, 30 (no overlaps, no carries)
      const uint32_t pairs = (((v >> (2 * j)) & 0x03030303u) * 0x01041040u) >> 24;
      const uint32_t clock = i * 4 + j;
      const uint32_t first = clock == 0 ? 0 : 8;
      const uint32_t lane0 = first | (((h >> (clock % 8)) & 1) << 2) | hv;
      out[2 + clock] = TERC4[lane0] | TERC4_LANES[pairs];
    }
  }
}

} // namespace hdmi
} // namespace hal

#endif // HAL_RP2350_HDMI_PACKET_H
