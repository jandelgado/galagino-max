#include <cstring>
#include <vector>
#include <unity.h>

// Asset<PLAIN> in an ASSETS_REMOTE build: a blob without data resolves its
// asset id in the mounted bundle, once, in the ctor.
#define ASSETS_REMOTE
#include "../../src/emulation/arena.cpp"
#include "../../src/emulation/any_asset.h"
#include "../../src/net/bundle.cpp"

void setUp(void) {}
void tearDown(void) { Bundle::unmount(); }

static const uint32_t ID = 0x1234;

static void put32(std::vector<uint8_t> &b, uint32_t at, uint32_t v) {
  for (uint32_t i = 0; i < 4; i++) {
    b[at + i] = (uint8_t)(v >> (8 * i));
  }
}

// one entry: id ID, 8 raw bytes at offset 28
static std::vector<uint8_t> oneEntry() {
  std::vector<uint8_t> b(36, 0);
  put32(b, 0, 0x42474C47);
  b[4] = 1;
  b[6] = 1;
  const uint32_t e[5] = {ID, 28, 8, 8, 0};
  for (uint32_t i = 0; i < 5; i++) {
    put32(b, 8 + 4 * i, e[i]);
  }
  return b;
}

static void test_builtin_blob_unchanged(void) {
  static const uint8_t arr[4] = {1, 2, 3, 4};
  static constexpr FlashAsset<uint8_t, PLAIN> blob = {arr, 4};
  Asset<uint8_t, PLAIN> a(blob);
  TEST_ASSERT_EQUAL_PTR(arr, a.data());
  TEST_ASSERT_EQUAL_UINT32(4, a.size());
}

static void test_remote_blob_resolves(void) {
  std::vector<uint8_t> b = oneEntry();
  TEST_ASSERT_TRUE(Bundle::mount(b.data(), b.size()));
  static constexpr FlashAsset<uint32_t[2], PLAIN> blob = {nullptr, 1, ID};
  Asset<uint32_t[2], PLAIN> a(blob);
  TEST_ASSERT_EQUAL_PTR(b.data() + 28, a.data());
}

static void test_remote_blob_missing(void) {
  std::vector<uint8_t> b = oneEntry();
  TEST_ASSERT_TRUE(Bundle::mount(b.data(), b.size()));
  static constexpr FlashAsset<uint32_t[2], PLAIN> blob = {nullptr, 1, 0x9999};
  Asset<uint32_t[2], PLAIN> a(blob);
  TEST_ASSERT_NULL(a.data());
}

// base classes fed by subclass blobs (system1, starforce) see remote blobs too
static void test_any_asset_remote_blob_resolves(void) {
  std::vector<uint8_t> b = oneEntry();
  TEST_ASSERT_TRUE(Bundle::mount(b.data(), b.size()));
  static constexpr FlashAsset<uint32_t[2], PLAIN> blob = {nullptr, 1, ID};
  const AnyAsset<uint32_t[2]> a{AssetRef<uint32_t[2]>(blob)};
  TEST_ASSERT_EQUAL_PTR(b.data() + 28, a.data());
  TEST_ASSERT_EQUAL_UINT32(1, a.size());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_builtin_blob_unchanged);
  RUN_TEST(test_remote_blob_resolves);
  RUN_TEST(test_remote_blob_missing);
  RUN_TEST(test_any_asset_remote_blob_resolves);
  return UNITY_END();
}
