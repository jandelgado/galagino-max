#ifndef SELFTEST_H
#define SELFTEST_H

#include <stdint.h>
#include "input.h"
#include "../config.h"

#ifdef LED_PIN
#include <FastLED.h>
#endif

#ifdef BOOT_SELFTEST
// arcade style boot screen: title, hardware info lines, crosshatch.
// sequence defined by PHASES in selftest.cpp
static constexpr uint16_t SELFTEST_WIDTH = 224;

// run the checks and start the self-test timer
void selftest_begin(Input *input, uint8_t games);

// advance the timer once per frame. false once the self-test is over
bool selftest_tick();

// draw strip `row` of the self-test screen into the cleared frame buffer
void selftest_render_row(uint16_t *frame_buffer, uint8_t row);

#ifdef LED_PIN
// LED color of the current phase, off once the self-test is over
CRGB selftest_led_color();
#endif
#endif

#endif // SELFTEST_H
