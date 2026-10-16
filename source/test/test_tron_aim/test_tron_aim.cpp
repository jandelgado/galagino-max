// Auto-aim dial against a model of the game's dial routine (ROM 0x0289):
// 7 bit counts, delta since the last read, 4 counts per aim step, aim
// angle 0-35 in RAM 0xc405 (0 = panel right, 9 = panel up, 10 deg steps).
#include <unity.h>

#include "../../src/emulation/input.h"
#include "../../src/machines/tron/tron_aim.h"

struct GameDial {
  uint8_t prev = 0;
  int8_t acc = 0;
  uint8_t angle = 0;

  void read(uint8_t dial) {
    const uint8_t d = dial & 0x7f;
    int delta = (int)d - (int)prev;
    prev = d;
    if (delta >= 64) {
      delta -= 128;
    } else if (delta < -64) {
      delta += 128;
    }
    acc += delta;
    while (acc >= 4) {
      acc -= 4;
      angle = angle == 35 ? 0 : angle + 1;
    }
    while (acc <= -4) {
      acc += 4;
      angle = angle == 0 ? 35 : angle - 1;
    }
  }
};

void setUp(void) {}
void tearDown(void) {}

// frames until the game angle settles on target, -1 if it never does
static int steer(TronAim &aim, GameDial &game, unsigned int keys, int target, int maxFrames) {
  for (int f = 0; f < maxFrames; f++) {
    aim.update(keys, game.angle);
    game.read(aim.dial());
    if (game.angle == target) {
      return f;
    }
  }
  return -1;
}

static void test_up_reached_from_right(void) {
  TronAim aim;
  GameDial game;
  TEST_ASSERT_GREATER_OR_EQUAL(0, steer(aim, game, BUTTON_UP, 9, 40));
}

static void test_down_reached_from_both_sides(void) {
  TronAim aim;
  GameDial game;
  game.angle = 20;
  TEST_ASSERT_GREATER_OR_EQUAL(0, steer(aim, game, BUTTON_DOWN, 27, 40));
  game.angle = 33;
  TEST_ASSERT_GREATER_OR_EQUAL(0, steer(aim, game, BUTTON_DOWN, 27, 40));
}

static void test_shortest_way_across_wrap(void) {
  TronAim aim;
  GameDial game;
  game.angle = 33;  // 330 deg, target right = 0: up through 34, 35, 0
  for (int f = 0; f < 10; f++) {
    aim.update(BUTTON_RIGHT, game.angle);
    game.read(aim.dial());
    TEST_ASSERT_TRUE(game.angle >= 33 || game.angle == 0);
  }
  TEST_ASSERT_EQUAL(0, game.angle);
}

static void test_diagonal_settles_between_steps(void) {
  TronAim aim;
  GameDial game;
  steer(aim, game, BUTTON_UP | BUTTON_RIGHT, -1, 40);  // 45 deg = 4.5 steps
  TEST_ASSERT_TRUE(game.angle == 4 || game.angle == 5);
  const uint8_t settled = aim.dial();
  aim.update(BUTTON_UP | BUTTON_RIGHT, game.angle);
  TEST_ASSERT_EQUAL(settled, aim.dial());  // no dithering
}

static void test_released_stick_keeps_dial(void) {
  TronAim aim;
  GameDial game;
  steer(aim, game, BUTTON_LEFT, 18, 40);
  const uint8_t d = aim.dial();
  aim.update(0, game.angle);
  aim.update(BUTTON_FIRE, game.angle);
  TEST_ASSERT_EQUAL(d, aim.dial());
}

static void test_one_step_per_frame(void) {
  TronAim aim;
  uint8_t last = aim.dial();
  for (int f = 0; f < 5; f++) {
    aim.update(BUTTON_LEFT, 0);
    const int delta = (int8_t)(uint8_t)(aim.dial() - last);
    TEST_ASSERT_TRUE(delta == 4 || delta == -4);
    last = aim.dial();
  }
}

static void test_shoulder_buttons_turn(void) {
  TronAim aim;
  const uint8_t d = aim.dial();
  aim.update(BUTTON_R1 | BUTTON_UP, 0);  // direct turn wins over the stick
  TEST_ASSERT_EQUAL((uint8_t)(d - 4), aim.dial());
  aim.update(BUTTON_L1, 0);
  aim.update(BUTTON_L1, 0);
  TEST_ASSERT_EQUAL((uint8_t)(d + 4), aim.dial());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_up_reached_from_right);
  RUN_TEST(test_down_reached_from_both_sides);
  RUN_TEST(test_shortest_way_across_wrap);
  RUN_TEST(test_diagonal_settles_between_steps);
  RUN_TEST(test_released_stick_keeps_dial);
  RUN_TEST(test_one_step_per_frame);
  RUN_TEST(test_shoulder_buttons_turn);
  return UNITY_END();
}
