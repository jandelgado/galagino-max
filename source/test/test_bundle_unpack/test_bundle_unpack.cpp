#include <cstring>
#include <vector>
#include <unity.h>

// BundleUnpacker (src/net/bundle_unpack.cpp): wire bundle in, cache entry
// out. fixture.h: romconv/internal/pyconv/make_unpack_fixture.py.
#include "../test_asset_remote/lzma.c"
#include "../../src/net/bundle_unpack.cpp"
#include "fixture.h"

static std::vector<uint8_t> out;
static bool refuse = false;
static uint8_t dict[BundleUnpacker::PLAIN_DICT_BYTES];

static bool collect(void *, const uint8_t *data, uint32_t length) {
  if (refuse) {
    return false;
  }
  out.insert(out.end(), data, data + length);
  return true;
}

static void *heapAlloc(ISzAllocPtr, size_t size) { return malloc(size); }
static void heapFree(ISzAllocPtr, void *p) { free(p); }
static const ISzAlloc alloc = {heapAlloc, heapFree};

void setUp(void) {
  out.clear();
  refuse = false;
}
void tearDown(void) {}

static uint32_t fnv1a(const std::vector<uint8_t> &d) {
  uint32_t h = 0x811C9DC5;
  for (uint8_t b : d) {
    h = (h ^ b) * 0x01000193;
  }
  return h;
}

static void put32(std::vector<uint8_t> &b, uint32_t at, uint32_t v) {
  for (uint32_t i = 0; i < 4; i++) {
    b[at + i] = (uint8_t)(v >> (8 * i));
  }
}

static uint32_t get32(const std::vector<uint8_t> &b, uint32_t at) {
  return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | ((uint32_t)b[at + 3] << 24);
}

// feeds data in chunks; false as soon as feed() refuses
static bool feedAll(BundleUnpacker &u, const uint8_t *data, uint32_t length, uint32_t chunk) {
  for (uint32_t at = 0; at < length; at += chunk) {
    const uint32_t n = length - at < chunk ? length - at : chunk;
    if (!u.feed(data + at, n)) {
      return false;
    }
  }
  return true;
}

static void checkWhole(uint32_t chunk) {
  BundleUnpacker u(collect, nullptr, dict, &alloc);
  TEST_ASSERT_EQUAL_UINT32(0, u.entryLength());
  TEST_ASSERT_TRUE(feedAll(u, wire, sizeof(wire), chunk));
  TEST_ASSERT_TRUE(u.finished());
  TEST_ASSERT_EQUAL_UINT32(ENTRY_LENGTH, u.entryLength());
  TEST_ASSERT_EQUAL_UINT32(ENTRY_LENGTH, out.size());
  TEST_ASSERT_EQUAL_HEX32(ENTRY_FNV, fnv1a(out));
}

static void test_whole_chunks(void) { checkWhole(sizeof(wire)); }
static void test_byte_chunks(void) { checkWhole(1); }
static void test_1460_chunks(void) { checkWhole(1460); }

// big entry decodes past the 64 KB dictionary: same bytes as gen() in the fixture script
static void test_big_entry_bytes(void) {
  checkWhole(97);
  const uint32_t offset = get32(out, 8 + 20 + 4);
  const uint32_t length = get32(out, 8 + 20 + 8);
  TEST_ASSERT_EQUAL_UINT32(70000, length);
  for (uint32_t i = 0; i < length; i++) {
    if (out[offset + i] != (uint8_t)((i * 7 + (i >> 11)) & 0xFF)) {
      TEST_FAIL_MESSAGE("decoded byte differs");
    }
  }
}

static void test_truncated_stream(void) {
  BundleUnpacker u(collect, nullptr, dict, &alloc);
  TEST_ASSERT_TRUE(feedAll(u, wire, sizeof(wire) - 1, 64));
  TEST_ASSERT_FALSE(u.finished());
}

static void test_trailing_bytes(void) {
  std::vector<uint8_t> w(wire, wire + sizeof(wire));
  w.push_back(0);
  BundleUnpacker u(collect, nullptr, dict, &alloc);
  TEST_ASSERT_FALSE(feedAll(u, w.data(), w.size(), w.size()));
}

static bool rejects(std::vector<uint8_t> w) {
  out.clear();
  BundleUnpacker u(collect, nullptr, dict, &alloc);
  return !feedAll(u, w.data(), w.size(), w.size());
}

static void test_bad_headers(void) {
  const std::vector<uint8_t> good(wire, wire + sizeof(wire));
  TEST_ASSERT_FALSE(rejects(good));

  std::vector<uint8_t> w = good;
  put32(w, 0, 0x12345678);
  TEST_ASSERT_TRUE_MESSAGE(rejects(w), "magic");

  w = good;
  w[4] = 1;
  TEST_ASSERT_TRUE_MESSAGE(rejects(w), "version 1");

  w = good;
  w[6] = 65;
  TEST_ASSERT_TRUE_MESSAGE(rejects(w), "65 entries");

  w = good;
  put32(w, 8 + 4, get32(w, 8 + 4) + 1);
  TEST_ASSERT_TRUE_MESSAGE(rejects(w), "misaligned offset");

  w = good;
  put32(w, 8 + 20 + 4, get32(w, 8 + 4));
  TEST_ASSERT_TRUE_MESSAGE(rejects(w), "overlapping entries");

  w = good;
  put32(w, 8 + 16, 0);
  TEST_ASSERT_TRUE_MESSAGE(rejects(w), "flag RAW on the wire");

  w = good;
  put32(w, 8 + 16, 3);
  TEST_ASSERT_TRUE_MESSAGE(rejects(w), "unknown flag");
}

static void test_sink_refuses(void) {
  refuse = true;
  BundleUnpacker u(collect, nullptr, dict, &alloc);
  TEST_ASSERT_FALSE(feedAll(u, wire, sizeof(wire), sizeof(wire)));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_whole_chunks);
  RUN_TEST(test_byte_chunks);
  RUN_TEST(test_1460_chunks);
  RUN_TEST(test_big_entry_bytes);
  RUN_TEST(test_truncated_stream);
  RUN_TEST(test_trailing_bytes);
  RUN_TEST(test_bad_headers);
  RUN_TEST(test_sink_refuses);
  return UNITY_END();
}
