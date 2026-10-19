#include <unity.h>
#include "../../src/machines/williams/williams_keys.h"
#include "../../src/machines/williams/robotron_sticks.h"

void setUp(void) {}
void tearDown(void) {}

void test_move_without_fire(void) {
  RobotronSticks s;
  const uint16_t v = s.update(BOARD_KEY_LEFT | BOARD_KEY_UP);
  TEST_ASSERT_EQUAL_HEX16(RobotronSticks::MOVE_LEFT | RobotronSticks::MOVE_UP, v);
}

void test_fire_locks_direction_while_held(void) {
  RobotronSticks s;
  s.update(BOARD_KEY_RIGHT);
  uint16_t v = s.update(BOARD_KEY_FIRE);  // centered: fire where we last moved
  TEST_ASSERT_EQUAL_HEX16(RobotronSticks::FIRE_RIGHT, v);
  v = s.update(BOARD_KEY_FIRE | BOARD_KEY_DOWN);  // move down, keep shooting right
  TEST_ASSERT_EQUAL_HEX16(RobotronSticks::MOVE_DOWN | RobotronSticks::FIRE_RIGHT, v);
}

void test_new_press_takes_stick_direction(void) {
  RobotronSticks s;
  s.update(BOARD_KEY_RIGHT);
  s.update(BOARD_KEY_FIRE);
  s.update(BOARD_KEY_UP);                 // release
  const uint16_t v = s.update(BOARD_KEY_FIRE | BOARD_KEY_UP);
  TEST_ASSERT_TRUE(v & RobotronSticks::FIRE_UP);
}

void test_controller_buttons_aim(void) {
  RobotronSticks s;
  const uint16_t v = s.update(BOARD_KEY_X | BOARD_KEY_B);  // up + right
  TEST_ASSERT_EQUAL_HEX16(RobotronSticks::FIRE_UP | RobotronSticks::FIRE_RIGHT, v);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_move_without_fire);
  RUN_TEST(test_fire_locks_direction_while_held);
  RUN_TEST(test_new_press_takes_stick_direction);
  RUN_TEST(test_controller_buttons_aim);
  return UNITY_END();
}
