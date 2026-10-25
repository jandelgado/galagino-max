// I2S sink for the ESP-IDF only build (standard mode driver): the ES8311
// codec, set up over I2C by Audio::init. Philips format, 16 bit signed mono in
// the left slot, MCLK = 256 x rate, as the codec is configured. Untested on a
// device (no IDF FNK0104 at hand); the Arduino build is the reference.
#include "../audio.h"
#if !defined(ARDUINO) && defined(HAL_AUDIO_ES8311)
#include <freertos/FreeRTOS.h>
#include <driver/i2s_std.h>
#include <string.h>

namespace hal {

namespace {

const uint32_t MAX_CHUNK = 64;

i2s_chan_handle_t tx = nullptr;
int16_t staged[MAX_CHUNK];
uint32_t chunkSamples = MAX_CHUNK;
uint32_t bufferCount = 4;

// One chunk is exactly one DMA buffer (dma_frame_num = chunk), so a write
// either finds a free buffer and takes it all, or times out writing nothing.
bool load(uint32_t timeoutMs) {
  size_t written = 0;
  i2s_channel_write(tx, staged, chunkSamples * sizeof(int16_t), &written, timeoutMs);
  return written != 0;
}

} // namespace

void AudioOut::init(const AudioConfig &cfg) {
  chunkSamples = cfg.chunk > MAX_CHUNK ? MAX_CHUNK : cfg.chunk;
  bufferCount = cfg.buffers;

  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.dma_desc_num = bufferCount;
  chan.dma_frame_num = chunkSamples;
  chan.auto_clear = true;  // underrun plays silence, not old buffers
  i2s_new_channel(&chan, &tx, nullptr);

  i2s_std_config_t std = {};
  std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cfg.rate);  // MCLK 256 x rate
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  std.gpio_cfg.mclk = (gpio_num_t)ES8311_I2S_MCK;
  std.gpio_cfg.bclk = (gpio_num_t)ES8311_I2S_BCK;
  std.gpio_cfg.ws = (gpio_num_t)ES8311_I2S_WS;
  std.gpio_cfg.dout = (gpio_num_t)ES8311_I2S_DOUT;
  std.gpio_cfg.din = I2S_GPIO_UNUSED;
  i2s_channel_init_std_mode(tx, &std);
  i2s_channel_enable(tx);

  flushSilence();
}

bool AudioOut::write(const int16_t *samples) {
  memcpy(staged, samples, chunkSamples * sizeof(int16_t));
  return load(0);
}

void AudioOut::writeBlocking(const int16_t *samples) {
  memcpy(staged, samples, chunkSamples * sizeof(int16_t));
  load(portMAX_DELAY);
}

// Signed codec: 0 is silence, no ramp needed.
void AudioOut::flushSilence() {
  memset(staged, 0, sizeof(staged));
  for (uint32_t i = 0; i < bufferCount; i++) {
    load(portMAX_DELAY);
  }
}

} // namespace hal

#endif // !ARDUINO && HAL_AUDIO_ES8311
