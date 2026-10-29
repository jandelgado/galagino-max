#include <unity.h>

// Bluetooth pad mapping (input/bt_gamepad) and Input::buttons_get() with the
// pad as its only source. Pad state: hal host backend (setGamepad).
#define HAL_GAMEPAD_BT
#define USE_PIO_CONFIG
#define BTN_START_PIN 1
#define BTN_LEFT_PIN  2
#define BTN_RIGHT_PIN 3
#define BTN_DOWN_PIN  4
#define BTN_UP_PIN    5
#define BTN_FIRE_PIN  6
#include "../../src/input/bt_gamepad.cpp"
#include "../../src/emulation/input.cpp"

using hal::GamepadState;

static const int PIN_COUNT = 8;

static GamepadState pad(uint16_t buttons, uint8_t dpad = 0, int16_t x = 0, int16_t y = 0) {
  GamepadState s = {};
  s.connected = true;
  s.buttons = buttons;
  s.dpad = dpad;
  s.x = x;
  s.y = y;
  return s;
}

void setUp(void) {
  for (int pin = 0; pin < PIN_COUNT; pin++) {
    hal::host::setPinLevel(pin, true);  // released, active low
  }
  hal::host::setMicros(0);
  hal::host::setGamepad(GamepadState());
}
void tearDown(void) {}

static void test_disconnected_is_zero(void) {
  GamepadState s = pad(GamepadState::A, GamepadState::UP);
  s.connected = false;
  TEST_ASSERT_EQUAL_HEX(0, btGamepadButtons(s));
}

static void test_dpad_directions(void) {
  TEST_ASSERT_EQUAL_HEX(BUTTON_UP, btGamepadButtons(pad(0, GamepadState::UP)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_DOWN, btGamepadButtons(pad(0, GamepadState::DOWN)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_LEFT, btGamepadButtons(pad(0, GamepadState::LEFT)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_RIGHT, btGamepadButtons(pad(0, GamepadState::RIGHT)));
}

// stick -512..511, y negative is up, threshold 128
static void test_stick_threshold(void) {
  TEST_ASSERT_EQUAL_HEX(BUTTON_LEFT, btGamepadButtons(pad(0, 0, -128, 0)));
  TEST_ASSERT_EQUAL_HEX(0, btGamepadButtons(pad(0, 0, -127, 0)));
  TEST_ASSERT_EQUAL_HEX(0, btGamepadButtons(pad(0, 0, 127, 0)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_RIGHT, btGamepadButtons(pad(0, 0, 128, 0)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_UP, btGamepadButtons(pad(0, 0, 0, -128)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_DOWN, btGamepadButtons(pad(0, 0, 0, 128)));
}

static void test_stick_and_dpad_or(void) {
  TEST_ASSERT_EQUAL_HEX(BUTTON_LEFT | BUTTON_RIGHT,
                        btGamepadButtons(pad(0, GamepadState::RIGHT, -300, 0)));
}

static void test_button_map(void) {
  TEST_ASSERT_EQUAL_HEX(BUTTON_FIRE, btGamepadButtons(pad(GamepadState::A)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_B, btGamepadButtons(pad(GamepadState::B)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_X, btGamepadButtons(pad(GamepadState::X)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_Y, btGamepadButtons(pad(GamepadState::Y)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_L1, btGamepadButtons(pad(GamepadState::L1)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_R1, btGamepadButtons(pad(GamepadState::R1)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_START, btGamepadButtons(pad(GamepadState::START)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_COIN | BUTTON_EXTRA, btGamepadButtons(pad(GamepadState::SELECT)));
  TEST_ASSERT_EQUAL_HEX(BUTTON_MENU, btGamepadButtons(pad(GamepadState::SYSTEM)));
  TEST_ASSERT_EQUAL_HEX(0, btGamepadButtons(pad(GamepadState::L2 | GamepadState::R2)));
}

// one input source per build: GPIO pins are not read
static void test_pad_replaces_gpio(void) {
  Input input;
  input.init(true);
  hal::host::setPinLevel(BTN_FIRE_PIN, false);
  hal::host::setGamepad(pad(0, GamepadState::LEFT));
  TEST_ASSERT_EQUAL_HEX(BUTTON_LEFT, input.buttons_get());
}

// fire held at boot switches demo sounds off and is ignored until released,
// pad fire included
static void test_fire_held_at_start_blocks_pad_fire(void) {
  hal::host::setGamepad(pad(GamepadState::A));
  Input input;
  input.init(true);
  TEST_ASSERT_TRUE(input.demoSoundsOff());
  TEST_ASSERT_EQUAL_HEX(0, input.buttons_get() & BUTTON_FIRE);
  hal::host::setGamepad(pad(0));
  input.buttons_get();
  hal::host::setGamepad(pad(GamepadState::A));
  TEST_ASSERT_EQUAL_HEX(BUTTON_FIRE, input.buttons_get() & BUTTON_FIRE);
}

// SELECT is the COIN button: held 3 s it ends the game (as a GPIO COIN pin)
static void test_select_held_resets(void) {
  Input input;
  input.init(false);
  bool reset = false;
  input.onDoReset([&reset]() { reset = true; });
  hal::host::setGamepad(pad(GamepadState::SELECT));
  hal::host::setMicros(1000);
  input.buttons_get();
  hal::host::setMicros(2000000);
  input.buttons_get();
  TEST_ASSERT_FALSE(reset);
  hal::host::setMicros(3200000);
  input.buttons_get();
  TEST_ASSERT_TRUE(reset);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_disconnected_is_zero);
  RUN_TEST(test_dpad_directions);
  RUN_TEST(test_stick_threshold);
  RUN_TEST(test_stick_and_dpad_or);
  RUN_TEST(test_button_map);
  RUN_TEST(test_pad_replaces_gpio);
  RUN_TEST(test_fire_held_at_start_blocks_pad_fire);
  RUN_TEST(test_select_held_resets);
  return UNITY_END();
}
