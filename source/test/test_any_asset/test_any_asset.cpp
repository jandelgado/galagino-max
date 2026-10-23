#include <unity.h>

// AnyAsset (src/emulation/any_asset.h): one asset from a PLAIN or a
// COMPRESSED blob, for base classes fed by subclass blobs.
#include "../../src/emulation/arena.cpp"
#include "../../src/emulation/any_asset.h"

static const unsigned char plainData[] = {9, 8, 7, 6};
static constexpr FlashAsset<unsigned char, PLAIN> plainBlob = {plainData, 4};

// romconv/romconv/emit/lzma.py lzma_compress(bytes([1, 2, 3, 4]))
static const unsigned char packed[] = {0xB4, 0x00, 0x00, 0x80, 0x9D, 0x61, 0xE5, 0x1A,
                                       0x5F, 0xFF, 0xFF, 0xF6, 0xEB, 0xC0, 0x00};
static constexpr FlashAsset<unsigned char, COMPRESSED> packedBlob = {packed, sizeof(packed), 4, ARENA_B};

void setUp(void) { Arena::reset(); }
void tearDown(void) {}

static void test_plain_blob(void) {
  const AnyAsset<unsigned char> a{AssetRef<unsigned char>(plainBlob)};
  TEST_ASSERT_EQUAL_PTR(plainData, a.data());
  TEST_ASSERT_EQUAL_UINT32(4, a.size());
}

static void test_omitted(void) {
  const AnyAsset<unsigned char> a{AssetRef<unsigned char>(nullptr)};
  TEST_ASSERT_NULL(a.data());
  TEST_ASSERT_EQUAL_UINT32(0, a.size());
}

// unpacks into the blob's planned block, inside the reservation
static void test_packed_blob_planned_block(void) {
  const uint8_t *baseA = Arena::alloc(1);
  const uint8_t *baseB = Arena::alloc(Arena::BLOCK_A_SIZE);
  (void)baseA;
  const ArenaPlan plan = {0, 8};
  Arena::reserve(&plan);

  const AnyAsset<unsigned char> a{AssetRef<unsigned char>(packedBlob)};
  const unsigned char expected[] = {1, 2, 3, 4};
  TEST_ASSERT_EQUAL_UINT32(4, a.size());
  TEST_ASSERT_EQUAL_MEMORY(expected, a.data(), 4);
  TEST_ASSERT_TRUE(a.data() >= baseB + Arena::BLOCK_B_SIZE - 8);
}

// LZMA table borrowed from the Arena (scratch) and given back: the next
// unpack finds the gap and the scratch free again
static void test_unpack_returns_scratch(void) {
  const Asset<unsigned char, COMPRESSED> a(packed, sizeof(packed), 4);
  const unsigned char expected[] = {1, 2, 3, 4};
  TEST_ASSERT_EQUAL_MEMORY(expected, a.data(), 4);
  uint8_t *s = Arena::scratch(100);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_TRUE(Arena::release(s));
  // only the 4 output bytes stay allocated
  TEST_ASSERT_EQUAL_UINT32(4, Arena::bytesUsed());
}

// no gap for the table (or the other core holds the scratch): heap fallback
static void test_unpack_without_scratch(void) {
  uint8_t *held = Arena::scratch(16);
  const Asset<unsigned char, COMPRESSED> a(packed, sizeof(packed), 4);
  const unsigned char expected[] = {1, 2, 3, 4};
  TEST_ASSERT_EQUAL_MEMORY(expected, a.data(), 4);
  TEST_ASSERT_TRUE(Arena::release(held));
}

int main(int, char **) {
  Arena::init();
  UNITY_BEGIN();
  RUN_TEST(test_plain_blob);
  RUN_TEST(test_omitted);
  RUN_TEST(test_packed_blob_planned_block);
  RUN_TEST(test_unpack_returns_scratch);
  RUN_TEST(test_unpack_without_scratch);
  return UNITY_END();
}
