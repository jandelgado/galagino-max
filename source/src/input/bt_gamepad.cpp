#include "bt_gamepad.h"

#ifdef HAL_GAMEPAD_BT
#include "buttons.h"

using hal::GamepadState;

// stick deflection that counts as a direction, a quarter of full range
static constexpr int16_t STICK_THRESHOLD = 128;

struct ButtonMap {
  uint16_t pad;
  uint16_t buttons;
};

static constexpr ButtonMap BUTTON_MAP[] = {
  // FIRE only: machines that read BUTTON_A give it an action of its own
  // (defender hyperspace, sinistar bomb), a pad shot would trigger both
  {GamepadState::A, BUTTON_FIRE},
  {GamepadState::B, BUTTON_B},
  {GamepadState::X, BUTTON_X},
  {GamepadState::Y, BUTTON_Y},
  {GamepadState::L1, BUTTON_L1},
  {GamepadState::R1, BUTTON_R1},
  {GamepadState::START, BUTTON_START},
  {GamepadState::SELECT, BUTTON_COIN | BUTTON_EXTRA},  // as a COIN pin: held 3 s ends the game
  {GamepadState::SYSTEM, BUTTON_MENU},
};

uint16_t btGamepadButtons(const GamepadState &s) {
  if (!s.connected) {
    return 0;
  }

  // dpad and stick both steer
  uint16_t bits = 0;
  if ((s.dpad & GamepadState::UP) || s.y <= -STICK_THRESHOLD) {
    bits |= BUTTON_UP;
  }
  if ((s.dpad & GamepadState::DOWN) || s.y >= STICK_THRESHOLD) {
    bits |= BUTTON_DOWN;
  }
  if ((s.dpad & GamepadState::LEFT) || s.x <= -STICK_THRESHOLD) {
    bits |= BUTTON_LEFT;
  }
  if ((s.dpad & GamepadState::RIGHT) || s.x >= STICK_THRESHOLD) {
    bits |= BUTTON_RIGHT;
  }

  for (const ButtonMap &m : BUTTON_MAP) {
    if (s.buttons & m.pad) {
      bits |= m.buttons;
    }
  }
  return bits;
}

uint16_t btGamepadInput() {
  GamepadState s;
  hal::Gamepad::read(s);
  return btGamepadButtons(s);
}

#endif
