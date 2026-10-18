#include <cstring>
#include <vector>
#include <unity.h>

// AssetStore commit logic (src/net/asset_store.cpp) against a RAM fake of
// NOR flash: erase sets 0xFF, a write may only hit erased bytes.
#include "../../src/net/bundle.cpp"
#include "../../src/net/asset_store.cpp"

static const uint32_t FAKE_SECTOR = 4096;

class FakeRegion : public FlashRegion {
public:
  explicit FakeRegion(uint32_t bytes) : mem(bytes, 0x00) { }

  uint32_t size() const override { return mem.size(); }

  bool erase(uint32_t offset, uint32_t length) override {
    if (mapped) {
      eraseWhileMapped = true;
    }
    if (offset % FAKE_SECTOR || length % FAKE_SECTOR || offset + length > mem.size()) {
      return false;
    }
    memset(&mem[offset], 0xFF, length);
    return true;
  }

  bool write(uint32_t offset, const uint8_t *data, uint32_t length) override {
    if (offset + length > mem.size()) {
      return false;
    }
    for (uint32_t i = 0; i < length; i++) {
      if (mem[offset + i] != 0xFF) {
        return false;  // NOR: not erased
      }
      mem[offset + i] = data[i];
    }
    return true;
  }

  const uint8_t *map() override {
    if (mapFails) {
      return nullptr;
    }
    mapped = true;
    return mem.data();
  }

  void unmap() override { mapped = false; }

  std::vector<uint8_t> mem;
  bool mapped = false;
  bool eraseWhileMapped = false;
  bool mapFails = false;  // missing partition or MMU window full
};

static const uint32_t REGION_BYTES = 64 * 1024;

static BundleRef ref(uint8_t tag, uint32_t length) {
  BundleRef r = {{tag, 2, 3, 4, 5, 6, 7, 8}, length, length, "T"};
  return r;
}

static std::vector<uint8_t> payload(uint32_t length, uint8_t seed) {
  std::vector<uint8_t> d(length);
  for (uint32_t i = 0; i < length; i++) {
    d[i] = (uint8_t)(i * 7 + seed);
  }
  return d;
}

static void writeAll(AssetStore &store, const std::vector<uint8_t> &d) {
  const uint32_t cut1 = d.size() / 3;
  const uint32_t cut2 = d.size() / 2 + 1;
  TEST_ASSERT_TRUE(store.append(d.data(), cut1));
  TEST_ASSERT_TRUE(store.append(d.data() + cut1, cut2 - cut1));
  TEST_ASSERT_TRUE(store.append(d.data() + cut2, d.size() - cut2));
}

void setUp(void) {}
void tearDown(void) {}

static void test_empty_region_holds_nothing(void) {
  FakeRegion region(REGION_BYTES);
  AssetStore store(region);
  TEST_ASSERT_FALSE(store.holds(ref(1, 1000)));
}

static void test_full_write_commit_holds(void) {
  FakeRegion region(REGION_BYTES);
  AssetStore store(region);
  const BundleRef a = ref(1, 10001);
  const std::vector<uint8_t> d = payload(a.wireLength, 1);
  TEST_ASSERT_TRUE(store.begin(a));
  writeAll(store, d);
  TEST_ASSERT_TRUE(store.commit());
  TEST_ASSERT_TRUE(store.holds(a));
  TEST_ASSERT_EQUAL_MEMORY(d.data(), store.bundle(), d.size());
}

static void test_partial_write_not_held(void) {
  FakeRegion region(REGION_BYTES);
  AssetStore store(region);
  const BundleRef a = ref(1, 10000);
  const std::vector<uint8_t> d = payload(a.wireLength, 1);
  TEST_ASSERT_TRUE(store.begin(a));
  TEST_ASSERT_TRUE(store.append(d.data(), a.wireLength / 2));
  TEST_ASSERT_FALSE(store.holds(a));
  TEST_ASSERT_FALSE(store.commit());
  TEST_ASSERT_FALSE(store.holds(a));
}

static void test_append_overflow_rejected(void) {
  FakeRegion region(REGION_BYTES);
  AssetStore store(region);
  const BundleRef a = ref(1, 100);
  const std::vector<uint8_t> d = payload(101, 1);
  TEST_ASSERT_TRUE(store.begin(a));
  TEST_ASSERT_FALSE(store.append(d.data(), d.size()));
}

static void test_too_large_rejected(void) {
  FakeRegion region(REGION_BYTES);
  AssetStore store(region);
  TEST_ASSERT_FALSE(store.begin(ref(1, REGION_BYTES - AssetStore::TAG_BYTES + 1)));
  TEST_ASSERT_TRUE(store.begin(ref(1, REGION_BYTES - AssetStore::TAG_BYTES)));
}

static void test_rewrite_invalidates_old(void) {
  FakeRegion region(REGION_BYTES);
  AssetStore store(region);
  const BundleRef a = ref(1, 5000);
  const BundleRef b = ref(2, 6000);
  TEST_ASSERT_TRUE(store.begin(a));
  writeAll(store, payload(a.wireLength, 1));
  TEST_ASSERT_TRUE(store.commit());

  // power loss while bundle b is half written
  TEST_ASSERT_TRUE(store.begin(b));
  const std::vector<uint8_t> d = payload(b.wireLength, 2);
  TEST_ASSERT_TRUE(store.append(d.data(), b.wireLength / 2));
  AssetStore afterReboot(region);
  TEST_ASSERT_FALSE(afterReboot.holds(a));
  TEST_ASSERT_FALSE(afterReboot.holds(b));
}

static void test_other_hash_not_held(void) {
  FakeRegion region(REGION_BYTES);
  AssetStore store(region);
  const BundleRef a = ref(1, 5000);
  TEST_ASSERT_TRUE(store.begin(a));
  writeAll(store, payload(a.wireLength, 1));
  TEST_ASSERT_TRUE(store.commit());
  TEST_ASSERT_FALSE(store.holds(ref(9, 5000)));
  TEST_ASSERT_FALSE(store.holds(ref(1, 5001)));
}

static void test_begin_unmaps(void) {
  FakeRegion region(REGION_BYTES);
  AssetStore store(region);
  const BundleRef a = ref(1, 5000);
  TEST_ASSERT_TRUE(store.begin(a));
  writeAll(store, payload(a.wireLength, 1));
  TEST_ASSERT_TRUE(store.commit());
  TEST_ASSERT_NOT_NULL(store.bundle());
  TEST_ASSERT_TRUE(store.begin(ref(2, 5000)));
  TEST_ASSERT_FALSE(region.eraseWhileMapped);
}

static void test_map_failure_holds_nothing(void) {
  FakeRegion region(REGION_BYTES);
  AssetStore store(region);
  const BundleRef a = ref(1, 5000);
  TEST_ASSERT_TRUE(store.begin(a));
  writeAll(store, payload(a.wireLength, 1));
  TEST_ASSERT_TRUE(store.commit());
  region.mapFails = true;
  TEST_ASSERT_FALSE(store.holds(a));
  TEST_ASSERT_NULL(store.bundle());
}

static void test_region_smaller_than_tag(void) {
  FakeRegion region(0);  // partition missing: size 0
  AssetStore store(region);
  TEST_ASSERT_FALSE(store.begin(ref(1, 100)));
  TEST_ASSERT_FALSE(store.holds(ref(1, 100)));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_region_holds_nothing);
  RUN_TEST(test_full_write_commit_holds);
  RUN_TEST(test_partial_write_not_held);
  RUN_TEST(test_append_overflow_rejected);
  RUN_TEST(test_too_large_rejected);
  RUN_TEST(test_rewrite_invalidates_old);
  RUN_TEST(test_other_hash_not_held);
  RUN_TEST(test_begin_unmaps);
  RUN_TEST(test_map_failure_holds_nothing);
  RUN_TEST(test_region_smaller_than_tag);
  return UNITY_END();
}
