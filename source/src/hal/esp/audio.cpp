// Arduino-ESP32 (IDF 4.4) sink on the legacy I2S driver: built-in DAC, or
// I2S to the ES8311 codec (set up by Audio::init). IDF 6: audio_dac.cpp,
// audio_i2s.cpp.
#include "../audio.h"
#if defined(ARDUINO) && (defined(HAL_AUDIO_ESP_DAC) || defined(HAL_AUDIO_ES8311))
#include <Arduino.h>
#include <driver/i2s.h>
#include <stdio.h>
#include <string.h>
#include "audio_encode.h"

namespace hal {

namespace {

const uint32_t MAX_CHUNK = 64;
// 16 x 64 samples = 43 ms at 24 kHz
const uint32_t RAMP_CHUNKS = 16;

#ifdef SND_DIFF
const uint32_t WORDS_PER_SAMPLE = 2;  // two channels
#else
const uint32_t WORDS_PER_SAMPLE = 1;
#endif

uint16_t dma[MAX_CHUNK * WORDS_PER_SAMPLE];  // staging buffer for one chunk
uint32_t words = MAX_CHUNK * WORDS_PER_SAMPLE;
uint32_t chunkSamples = MAX_CHUNK;
uint32_t bufferCount = 4;

void writeStagedBlocking() {
  size_t bytesOut = 0;
  i2s_write(I2S_NUM_0, dma, words * sizeof(uint16_t), &bytesOut, portMAX_DELAY);
}

// Encodes `samples` into the staging buffer.
void stage(const int16_t *samples) {
#ifdef SND_DIFF
  esp::encodeDiff(samples, dma, chunkSamples);
#else
  esp::encodeMono(samples, dma, chunkSamples);
#endif
}

// Output sits at 0 after boot. Move it to SILENCE slowly: a jump clicks.
void rampToSilence() {
  const uint32_t steps = RAMP_CHUNKS * words;

  for (uint32_t chunk = 0; chunk < RAMP_CHUNKS; chunk++) {
    for (uint32_t i = 0; i < words; i++) {
      const uint32_t level = esp::SILENCE * (chunk * words + i) / steps;
#ifdef HAL_AUDIO_ES8311
      // signed codec: SILENCE is -32768, ramp 0 down to it
      dma[i] = (uint16_t)(0x10000 - level);
#else
      dma[i] = (uint16_t)level;
#endif
    }
    writeStagedBlocking();
  }

  AudioOut::flushSilence();
}

} // namespace

void AudioOut::init(const AudioConfig &cfg) {
  chunkSamples = cfg.chunk > MAX_CHUNK ? MAX_CHUNK : cfg.chunk;
  words = chunkSamples * WORDS_PER_SAMPLE;
  bufferCount = cfg.buffers;

#ifdef HAL_AUDIO_ES8311
  const i2s_pin_config_t pin_config = {
    .mck_io_num = ES8311_I2S_MCK,
    .bck_io_num = ES8311_I2S_BCK,
    .ws_io_num = ES8311_I2S_WS,
    .data_out_num = ES8311_I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE,
  };

  const i2s_config_t i2s_config = {
    .mode =                 (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate =          cfg.rate,
    .bits_per_sample =      I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format =       I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_MSB,
    .intr_alloc_flags = 0,
    .dma_buf_count = (int)cfg.buffers,
    .dma_buf_len = (int)cfg.chunk,
    .use_apll = false,
    .fixed_mclk = 0
  };

  esp_err_t i2s_err;
  i2s_err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  printf("es8311: i2s_driver_install err=%d\n", i2s_err);

  i2s_err = i2s_set_pin(I2S_NUM_0, &pin_config);
  printf("es8311: i2s_set_pin err=%d\n", i2s_err);

#elif CONFIG_IDF_TARGET_ESP32
  // 24 kHz @ 16 bit = 48000 bytes/sec = 800 bytes per 60hz game frame =
  // 1600 bytes per 30hz screen update = ~177 bytes every four tile rows
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
    .sample_rate = cfg.rate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
#ifdef SND_DIFF
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
#elif defined(SND_LEFT_CHANNEL) // For devices using the left channel (e.g. CYD)
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
#else
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
#endif
    .intr_alloc_flags = 0,
    .dma_buf_count = (int)cfg.buffers,
    .dma_buf_len = (int)cfg.chunk,   // 64 samples
    // APLL usage is broken in ESP-IDF 4.4.5
#ifdef WORKAROUND_I2S_APLL_PROBLEM
    .use_apll = false
#else
    .use_apll = true
#endif
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);

#ifdef SND_DIFF
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
#elif defined(SND_LEFT_CHANNEL) // For devices using the left channel (e.g. CYD)
  i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
#else
  i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);
#endif

#elif CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3 doesn't have an internal DAC, so...
#endif

  rampToSilence();
}

bool AudioOut::write(const int16_t *samples) {
  // We write data in exact the size of the DMA buffers, so either all or
  // nothing is actually written.
  stage(samples);
  size_t bytesOut = 0;
  i2s_write(I2S_NUM_0, dma, words * sizeof(uint16_t), &bytesOut, 0);
  return bytesOut != 0;
}

void AudioOut::writeBlocking(const int16_t *samples) {
  stage(samples);
  writeStagedBlocking();
}

// Fill all DMA buffers with silence. Without writes (self-test, machine
// load) the DMA replays them in a loop: old content hums.
void AudioOut::flushSilence() {
  for (uint32_t i = 0; i < words; i++) {
    dma[i] = esp::SILENCE;
  }
  for (uint32_t i = 0; i < bufferCount; i++) {
    writeStagedBlocking();
  }
}

} // namespace hal
#endif // ARDUINO && (HAL_AUDIO_ESP_DAC || HAL_AUDIO_ES8311)
