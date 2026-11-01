#include <cstdio>
#include <cstring>
#include <unity.h>

// Sha256 (src/net/sha256.cpp) against FIPS 180-4 example vectors, fed whole
// and in uneven chunks, as recv() delivers a download.
#include "../../src/net/sha256.cpp"

void setUp(void) {}
void tearDown(void) {}

static void hex(const uint8_t *digest, char *out) {
  for (uint32_t i = 0; i < Sha256::DIGEST_BYTES; i++) {
    snprintf(out + 2 * i, 3, "%02x", digest[i]);
  }
}

static void check(const uint8_t *data, uint32_t length, uint32_t chunk, const char *expected) {
  Sha256 sha;
  for (uint32_t pos = 0; pos < length; pos += chunk) {
    sha.update(data + pos, length - pos < chunk ? length - pos : chunk);
  }
  uint8_t digest[Sha256::DIGEST_BYTES];
  sha.finish(digest);
  char got[2 * Sha256::DIGEST_BYTES + 1];
  hex(digest, got);
  TEST_ASSERT_EQUAL_STRING(expected, got);
}

static const char *ABC = "abc";
static const char *ABC_HASH = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
static const char *EMPTY_HASH = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
// 56 bytes: padding needs a second block
static const char *TWO_BLOCK = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static const char *TWO_BLOCK_HASH = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
static const char *MILLION_A_HASH = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";

void test_abc(void) {
  check((const uint8_t *)ABC, 3, 3, ABC_HASH);
  check((const uint8_t *)ABC, 3, 1, ABC_HASH);
}

void test_empty(void) {
  Sha256 sha;
  uint8_t digest[Sha256::DIGEST_BYTES];
  sha.finish(digest);
  char got[2 * Sha256::DIGEST_BYTES + 1];
  hex(digest, got);
  TEST_ASSERT_EQUAL_STRING(EMPTY_HASH, got);
}

void test_two_blocks(void) {
  const uint32_t n = strlen(TWO_BLOCK);
  check((const uint8_t *)TWO_BLOCK, n, n, TWO_BLOCK_HASH);
  check((const uint8_t *)TWO_BLOCK, n, 7, TWO_BLOCK_HASH);
}

void test_million_a_in_chunks(void) {
  static uint8_t a[1000000];
  memset(a, 'a', sizeof(a));
  check(a, sizeof(a), sizeof(a), MILLION_A_HASH);
  check(a, sizeof(a), 4096, MILLION_A_HASH);
  check(a, sizeof(a), 1460, MILLION_A_HASH);  // TCP segment size
  check(a, sizeof(a), 63, MILLION_A_HASH);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_abc);
  RUN_TEST(test_empty);
  RUN_TEST(test_two_blocks);
  RUN_TEST(test_million_a_in_chunks);
  return UNITY_END();
}
