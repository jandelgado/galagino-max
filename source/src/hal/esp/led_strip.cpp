// WS2812 strip through the RMT peripheral, ESP-IDF only build. GRB color
// order (FastLED NEOPIXEL), global brightness LED_BRIGHTNESS.
#ifndef ARDUINO
#include "led.h"
#include "../../config.h"

#ifdef LED_PIN
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>

namespace hal {

namespace {

const uint32_t MAX_LEDS = 64;
const uint32_t RESOLUTION_HZ = 10000000;  // 0.1 us per tick

rmt_channel_handle_t channel = nullptr;
rmt_encoder_handle_t encoder = nullptr;
uint8_t grb[MAX_LEDS * 3];

} // namespace

void LedStrip::init(int, uint32_t) {
  if (channel) {
    return;
  }
  rmt_tx_channel_config_t tx = {};
  tx.gpio_num = (gpio_num_t)LED_PIN;
  tx.clk_src = RMT_CLK_SRC_DEFAULT;
  tx.resolution_hz = RESOLUTION_HZ;
  tx.mem_block_symbols = 64;
  tx.trans_queue_depth = 1;
  rmt_new_tx_channel(&tx, &channel);

  // WS2812: bit 0 = 0.3 us high + 0.9 us low, bit 1 = 0.9 us high + 0.3 us low
  rmt_bytes_encoder_config_t bytes = {};
  bytes.bit0.level0 = 1;
  bytes.bit0.duration0 = 3;
  bytes.bit0.level1 = 0;
  bytes.bit0.duration1 = 9;
  bytes.bit1.level0 = 1;
  bytes.bit1.duration0 = 9;
  bytes.bit1.level1 = 0;
  bytes.bit1.duration1 = 3;
  bytes.flags.msb_first = 1;
  rmt_new_bytes_encoder(&bytes, &encoder);
  rmt_enable(channel);
}

void LedStrip::show(const uint8_t *rgb, uint32_t count) {
  if (!channel) {
    return;
  }
  if (count > MAX_LEDS) {
    count = MAX_LEDS;
  }
  const uint16_t scale = (uint16_t)LED_BRIGHTNESS + 1;
  for (uint32_t i = 0; i < count; i++) {
    grb[3 * i] = (uint8_t)((rgb[3 * i + 1] * scale) >> 8);
    grb[3 * i + 1] = (uint8_t)((rgb[3 * i] * scale) >> 8);
    grb[3 * i + 2] = (uint8_t)((rgb[3 * i + 2] * scale) >> 8);
  }
  rmt_transmit_config_t cfg = {};
  rmt_transmit(channel, encoder, grb, count * 3, &cfg);
  rmt_tx_wait_all_done(channel, -1);  // grb stays valid until sent; the reset gap follows
}

} // namespace hal

#else

namespace hal {

void LedStrip::init(int, uint32_t) {}
void LedStrip::show(const uint8_t *, uint32_t) {}

} // namespace hal

#endif // LED_PIN
#endif // !ARDUINO
