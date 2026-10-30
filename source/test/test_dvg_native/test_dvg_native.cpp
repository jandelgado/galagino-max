#include <unity.h>

// dvgMachine's packed GO list line on native (640 x 480) builds
#define HAL_VIDEO_NATIVE 1
#include "../../src/machines/machineBase.h"
#include "../../src/cpus/m6502/m6502.h"
#include "../../src/emulation/seqlock.h"
#include "../../src/emulation/vector2d.h"
#include "../../src/emulation/dvg.h"
#define private public
#include "../../src/machines/dvgMachine.h"
#undef private

void setUp(void) {}
void tearDown(void) {}

static void test_packed_line_native(void) {
  TEST_ASSERT_EQUAL(8, sizeof(dvgMachine::PackedLine));

  dvgMachine::PackedLine p = {};
  p.x0 = 639;
  p.y0 = 479;
  p.x1 = 1;
  p.y1 = 2;
  p.color = 255;
  TEST_ASSERT_EQUAL(639, p.x0);
  TEST_ASSERT_EQUAL(479, p.y0);
  TEST_ASSERT_EQUAL(1, p.x1);
  TEST_ASSERT_EQUAL(2, p.y1);
  TEST_ASSERT_EQUAL(255, p.color);

  p.x1 = hal::VIDEO_NATIVE_W - 1;
  p.y1 = hal::VIDEO_NATIVE_H - 1;
  p.x0 = 0;
  TEST_ASSERT_EQUAL(639, p.x1);
  TEST_ASSERT_EQUAL(479, p.y1);
  TEST_ASSERT_EQUAL(0, p.x0);
  TEST_ASSERT_EQUAL(479, p.y0);
  TEST_ASSERT_EQUAL(255, p.color);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_packed_line_native);
  return UNITY_END();
}
