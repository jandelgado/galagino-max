#include <unity.h>
#include "../../src/machines/williams/williams_keys.h"
#include "../../src/machines/williams/defender_controls.h"

void setUp(void) {}
void tearDown(void) {}

static const unsigned int ACTION = BOARD_KEY_EXTRA;

void test_push_in_facing_direction_thrusts(void) {
  DefenderControls c;
  const uint16_t v = c.update(BOARD_KEY_RIGHT, false, ACTION);
  TEST_ASSERT_EQUAL_HEX16(DefenderControls::THRUST, v & 0xff);
}

void test_push_against_facing_reverses(void) {
  DefenderControls c;
  const uint16_t v = c.update(BOARD_KEY_LEFT, false, ACTION);
  TEST_ASSERT_EQUAL_HEX16(DefenderControls::REVERSE, v & 0xff);
}

void test_up_down_fire(void) {
  DefenderControls c;
  const uint16_t v = c.update(BOARD_KEY_UP | BOARD_KEY_FIRE, false, ACTION);
  TEST_ASSERT_EQUAL_HEX16(DefenderControls::FIRE, v & 0xff);
  TEST_ASSERT_EQUAL_HEX16(DefenderControls::UP, v >> 8);
  TEST_ASSERT_EQUAL_HEX16(DefenderControls::DOWN, c.update(BOARD_KEY_DOWN, false, ACTION) & 0xff);
}

void test_action_edge_smart_bomb_pulse(void) {
  DefenderControls c;
  int frames = 0;
  for (int i = 0; i < 10; i++) {
    if (c.update(ACTION, false, ACTION) & DefenderControls::SMART_BOMB) {
      frames++;
    }
  }
  TEST_ASSERT_EQUAL(DefenderControls::PULSE_FRAMES, frames);
}

// edge frame carries no pulse: START1 (same button) must rise first
void test_action_pulse_starts_frame_late(void) {
  DefenderControls c;
  TEST_ASSERT_FALSE(c.update(ACTION, false, ACTION) & DefenderControls::SMART_BOMB);
  TEST_ASSERT_TRUE(c.update(ACTION, false, ACTION) & DefenderControls::SMART_BOMB);
}

void test_action_with_direction_hyperspace(void) {
  DefenderControls c;
  c.update(ACTION | BOARD_KEY_LEFT, true, ACTION);
  const uint16_t v = c.update(ACTION | BOARD_KEY_LEFT, true, ACTION);
  TEST_ASSERT_TRUE(v & DefenderControls::HYPERSPACE);
  TEST_ASSERT_FALSE(v & DefenderControls::SMART_BOMB);
}

void test_bomb_active_during_pulse_only(void) {
  DefenderControls c;
  c.update(ACTION, false, ACTION);
  TEST_ASSERT_TRUE(c.bombActive());
  for (int i = 0; i < DefenderControls::PULSE_FRAMES; i++) {
    c.update(0, false, ACTION);
  }
  TEST_ASSERT_FALSE(c.bombActive());
  c.update(ACTION | BOARD_KEY_LEFT, false, ACTION);  // hyperspace, not a bomb
  TEST_ASSERT_FALSE(c.bombActive());
}

void test_facing_from_ram(void) {
  TEST_ASSERT_FALSE(DefenderControls::facingLeft(0x03));  // +3: right
  TEST_ASSERT_TRUE(DefenderControls::facingLeft(0xfd));   // -3: left
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_push_in_facing_direction_thrusts);
  RUN_TEST(test_push_against_facing_reverses);
  RUN_TEST(test_up_down_fire);
  RUN_TEST(test_action_edge_smart_bomb_pulse);
  RUN_TEST(test_action_pulse_starts_frame_late);
  RUN_TEST(test_action_with_direction_hyperspace);
  RUN_TEST(test_bomb_active_during_pulse_only);
  RUN_TEST(test_facing_from_ram);
  return UNITY_END();
}
