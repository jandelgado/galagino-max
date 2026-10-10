#include <string.h>
#include <unity.h>

#include "../../src/emulation/logo.cpp"
// exact (6 colors), plain 16 colors, gradient-heavy
#include "../../src/machines/baluba/baluba_logo.h"
#include "../../src/machines/pacman/pacman_logo.h"
#include "../../src/machines/roadfighter/roadfighter_logo.h"

static uint16_t slot[Logo::PIXELS];
static uint8_t packed[Logo::PACKED_BYTES];
static uint16_t expected[Logo::PIXELS];

void setUp(void) {}
void tearDown(void) {}

// out-of-place reference: inflate to its own buffer, then expand
static void decode_reference(const FlashAsset<uint8_t, COMPRESSED> &blob) {
  Asset<uint8_t, COMPRESSED>(blob).decodeInto(packed);

  uint16_t palette[Logo::COLORS];
  memcpy(palette, packed, sizeof(palette));
  for (uint32_t i = 0; i < Logo::PIXELS; i++) {
    const uint8_t pair = packed[sizeof(palette) + i / 2];
    expected[i] = palette[(i & 1) ? (pair & 0x0f) : (pair >> 4)];
  }
}

static void check(const Logo &logo, const FlashAsset<uint8_t, COMPRESSED> &blob) {
  TEST_ASSERT_EQUAL_UINT32(Logo::PACKED_BYTES, blob.count);

  decode_reference(blob);
  memset(slot, 0xa5, sizeof(slot));
  logo.decodeInto(slot);
  TEST_ASSERT_EQUAL_UINT16_ARRAY(expected, slot, Logo::PIXELS);
}

static void test_baluba(void) { check(baluba_logo, baluba_logo_blob); }
static void test_pacman(void) { check(pacman_logo, pacman_logo_blob); }
static void test_roadfighter(void) { check(roadfighter_logo, roadfighter_logo_blob); }

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_baluba);
  RUN_TEST(test_pacman);
  RUN_TEST(test_roadfighter);
  return UNITY_END();
}
