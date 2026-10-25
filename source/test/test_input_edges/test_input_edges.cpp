#include <unity.h>

// Rising-edge detection in Input::buttons_get() for the attract reset.
// Arduino.h in this directory is a host stub.
#define USE_PIO_CONFIG
#define BTN_START_PIN 1
#define BTN_LEFT_PIN  2
#define BTN_RIGHT_PIN 3
#define BTN_DOWN_PIN  4
#define BTN_UP_PIN    5
#define BTN_FIRE_PIN  6
#include "../../src/emulation/input.cpp"

static const int PIN_COUNT = 8;

static bool pressed[PIN_COUNT];
static unsigned long now;
static int attractResets;
static Input input;

void pinMode(int, int) {}
int digitalRead(int pin) { return pressed[pin] ? 0 : 1; }
unsigned long millis() { return now; }

void setUp(void) {
  memset(pressed, 0, sizeof(pressed));
  now = 0;
  attractResets = 0;
  input = Input();
  input.onDoAttractReset([]() { attractResets++; });
  input.init(false);
}
void tearDown(void) {}

// fire is not an attract-reset trigger
void test_fire_does_not_reset_attract(void) {
  pressed[BTN_FIRE_PIN] = true;
  input.buttons_get();
  TEST_ASSERT_EQUAL(0, attractResets);
}

// stick up: one reset on the press, none while held
void test_up_edge_resets_once(void) {
  pressed[BTN_UP_PIN] = true;
  input.buttons_get();
  input.buttons_get();
  TEST_ASSERT_EQUAL(1, attractResets);
}

// a second direction pressed while up is held is its own edge
void test_down_while_up_held(void) {
  pressed[BTN_UP_PIN] = true;
  input.buttons_get();
  pressed[BTN_DOWN_PIN] = true;
  input.buttons_get();
  TEST_ASSERT_EQUAL(2, attractResets);
}

// fire pressed while up is held is not an edge of a trigger button
void test_fire_while_up_held(void) {
  pressed[BTN_UP_PIN] = true;
  input.buttons_get();
  pressed[BTN_FIRE_PIN] = true;
  input.buttons_get();
  TEST_ASSERT_EQUAL(1, attractResets);
}

// start doubles as EXTRA without a coin pin
void test_start_edge_resets_once(void) {
  pressed[BTN_START_PIN] = true;
  input.buttons_get();
  input.buttons_get();
  TEST_ASSERT_EQUAL(1, attractResets);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_fire_does_not_reset_attract);
  RUN_TEST(test_up_edge_resets_once);
  RUN_TEST(test_down_while_up_held);
  RUN_TEST(test_fire_while_up_held);
  RUN_TEST(test_start_edge_resets_once);
  return UNITY_END();
}
