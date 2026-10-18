#include <cstring>
#include <vector>
#include <unity.h>

// AssetCache FIFO and commit logic (src/net/asset_cache.cpp) against a RAM
// fake of NOR flash: erase sets 0xFF, a write may only hit erased bytes.
#include "../../src/net/asset_cache.cpp"

static const uint32_t FAKE_SECTOR = 4096;
static const uint32_t REGION_BYTES = 1024 * 1024;
static const uint32_t KB = 1024;

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
    mapped = true;
    return mem.data();
  }

  void unmap() override { mapped = false; }

  std::vector<uint8_t> mem;
  bool mapped = false;
  bool eraseWhileMapped = false;
};

void setUp(void) {}
void tearDown(void) {}

static BundleRef ref(uint8_t tag, uint32_t entryLength) {
  BundleRef r = {{tag, 2, 3, 4, 5, 6, 7, 8}, 1000, entryLength, "T"};
  return r;
}

static std::vector<uint8_t> payload(uint32_t length, uint8_t seed) {
  std::vector<uint8_t> d(length);
  for (uint32_t i = 0; i < length; i++) {
    d[i] = (uint8_t)(i * 7 + seed);
  }
  return d;
}

// begin, append in 3 pieces, commit
static void store(AssetCache &cache, const BundleRef &r) {
  const std::vector<uint8_t> d = payload(r.entryLength, r.hash[0]);
  TEST_ASSERT_TRUE(cache.begin(r));
  const uint32_t cut1 = d.size() / 3;
  const uint32_t cut2 = d.size() / 2 + 1;
  TEST_ASSERT_TRUE(cache.append(d.data(), cut1));
  TEST_ASSERT_TRUE(cache.append(d.data() + cut1, cut2 - cut1));
  TEST_ASSERT_TRUE(cache.append(d.data() + cut2, d.size() - cut2));
  TEST_ASSERT_TRUE(cache.commit());
}

static void expectFound(AssetCache &cache, const BundleRef &r) {
  const uint8_t *p = cache.find(r);
  TEST_ASSERT_NOT_NULL(p);
  const std::vector<uint8_t> d = payload(r.entryLength, r.hash[0]);
  TEST_ASSERT_EQUAL_MEMORY(d.data(), p, d.size());
}

static void test_find_after_commit(void) {
  FakeRegion region(REGION_BYTES);
  AssetCache cache(region);
  const BundleRef a = ref(1, 10001);
  TEST_ASSERT_NULL(cache.find(a));
  store(cache, a);
  expectFound(cache, a);
  // other hash or length: not this entry
  TEST_ASSERT_NULL(cache.find(ref(2, 10001)));
  TEST_ASSERT_NULL(cache.find(ref(1, 10000)));
}

static void test_survives_new_instance(void) {
  FakeRegion region(REGION_BYTES);
  {
    AssetCache cache(region);
    store(cache, ref(1, 5000));
    store(cache, ref(2, 7000));
  }
  AssetCache cache(region);  // power cycle
  expectFound(cache, ref(1, 5000));
  expectFound(cache, ref(2, 7000));
}

static void test_uncommitted_entry_invisible(void) {
  FakeRegion region(REGION_BYTES);
  {
    AssetCache cache(region);
    store(cache, ref(1, 5000));
    const BundleRef b = ref(2, 9000);
    const std::vector<uint8_t> d = payload(b.entryLength, 2);
    TEST_ASSERT_TRUE(cache.begin(b));
    TEST_ASSERT_TRUE(cache.append(d.data(), d.size() / 2));
    TEST_ASSERT_FALSE(cache.commit());  // not every byte appended
  }
  AssetCache cache(region);  // power lost before commit
  TEST_ASSERT_NULL(cache.find(ref(2, 9000)));
  expectFound(cache, ref(1, 5000));
}

static void test_append_overflow_rejected(void) {
  FakeRegion region(REGION_BYTES);
  AssetCache cache(region);
  const std::vector<uint8_t> d = payload(101, 1);
  TEST_ASSERT_TRUE(cache.begin(ref(1, 100)));
  TEST_ASSERT_FALSE(cache.append(d.data(), d.size()));
}

static void test_fifo_wrap(void) {
  FakeRegion region(REGION_BYTES);
  AssetCache cache(region);
  const BundleRef a = ref(1, 300 * KB), b = ref(2, 300 * KB), c = ref(3, 300 * KB);
  store(cache, a);
  store(cache, b);
  store(cache, c);
  expectFound(cache, a);

  // no room after c: wraps to 0, evicts the oldest only
  const BundleRef d = ref(4, 300 * KB);
  store(cache, d);
  TEST_ASSERT_NULL(cache.find(a));
  expectFound(cache, b);
  expectFound(cache, c);
  expectFound(cache, d);

  // 750 KB after d overlaps b and c
  const BundleRef e = ref(5, 750 * KB);
  store(cache, e);
  TEST_ASSERT_NULL(cache.find(b));
  TEST_ASSERT_NULL(cache.find(c));
  TEST_ASSERT_NULL(cache.find(d));
  expectFound(cache, e);

  // the next one goes after e, which is the newest
  const BundleRef f = ref(6, 100 * KB);
  store(cache, f);
  expectFound(cache, e);
  expectFound(cache, f);
}

// wrap, then slots that cut into old entries: each overlapped entry goes
static void test_overlap_evicts_tag(void) {
  FakeRegion region(REGION_BYTES);
  AssetCache cache(region);
  const BundleRef a = ref(1, 600 * KB), b = ref(2, 300 * KB);
  store(cache, a);  // 0..604 KB
  store(cache, b);  // 604..908 KB
  // no room for 204 KB after b: wraps to 0, cuts into a only
  const BundleRef c = ref(3, 200 * KB);
  store(cache, c);  // 0..204 KB
  TEST_ASSERT_NULL(cache.find(a));
  expectFound(cache, b);
  expectFound(cache, c);

  const BundleRef d = ref(4, 380 * KB);
  store(cache, d);  // 204..588 KB, a's old data only
  expectFound(cache, b);

  // 588..692 KB cuts b (tag at 604 KB) in its first part
  const BundleRef e = ref(5, 100 * KB);
  store(cache, e);
  TEST_ASSERT_NULL(cache.find(b));
  expectFound(cache, c);
  expectFound(cache, d);
  expectFound(cache, e);
}

static void test_too_big(void) {
  FakeRegion region(REGION_BYTES);
  AssetCache cache(region);
  TEST_ASSERT_FALSE(cache.begin(ref(1, REGION_BYTES)));
  TEST_ASSERT_TRUE(cache.begin(ref(1, REGION_BYTES - AssetCache::TAG_BYTES)));
}

static void test_erase_unmapped(void) {
  FakeRegion region(REGION_BYTES);
  AssetCache cache(region);
  store(cache, ref(1, 600 * KB));
  store(cache, ref(2, 600 * KB));
  TEST_ASSERT_NOT_NULL(cache.find(ref(2, 600 * KB)));
  store(cache, ref(3, 600 * KB));
  TEST_ASSERT_FALSE(region.eraseWhileMapped);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_find_after_commit);
  RUN_TEST(test_survives_new_instance);
  RUN_TEST(test_uncommitted_entry_invisible);
  RUN_TEST(test_append_overflow_rejected);
  RUN_TEST(test_fifo_wrap);
  RUN_TEST(test_overlap_evicts_tag);
  RUN_TEST(test_too_big);
  RUN_TEST(test_erase_unmapped);
  return UNITY_END();
}
