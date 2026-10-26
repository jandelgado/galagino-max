// Built-in DAC sink: dac_continuous, asynchronous
// writing. The DAC replays its buffers in a ring; the driver reports each
// buffer it finished, which is the "free buffer" the sink hands out.
#include "../audio.h"
#if defined(HAL_AUDIO_ESP_DAC)
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <sdkconfig.h>
#include <soc/soc_caps.h>
#include "audio_encode.h"

#if SOC_DAC_SUPPORTED
#include <driver/dac_continuous.h>

namespace hal {

namespace {

const uint32_t MAX_CHUNK = 64;
// 16 x 64 samples = 43 ms at 24 kHz
const uint32_t RAMP_CHUNKS = 16;

#ifdef SND_DIFF
const uint32_t CHANNELS = 2;
#else
const uint32_t CHANNELS = 1;
#endif

dac_continuous_handle_t dac = nullptr;
QueueHandle_t freeBuffers = nullptr;  // uint8_t * of buffers the DAC finished
uint8_t staged[MAX_CHUNK * CHANNELS];
uint32_t chunkSamples = MAX_CHUNK;
uint32_t bufferCount = 4;
uint32_t bufferBytes = MAX_CHUNK * CHANNELS * 2;

bool IRAM_ATTR onConvertDone(dac_continuous_handle_t, const dac_event_data_t *event, void *) {
  BaseType_t woken = pdFALSE;
  uint8_t *buf = (uint8_t *)event->buf;
  xQueueSendFromISR(freeBuffers, &buf, &woken);
  return woken == pdTRUE;
}

// Loads the staged chunk into a DAC buffer. timeoutTicks 0: only if free.
bool load(TickType_t timeoutTicks) {
  uint8_t *buf;
  if (xQueueReceive(freeBuffers, &buf, timeoutTicks) != pdTRUE) {
    return false;
  }
  size_t loaded = 0;
  dac_continuous_write_asynchronously(dac, buf, bufferBytes, staged, chunkSamples * CHANNELS, &loaded);
  return true;
}

void stage(const int16_t *samples) {
#ifdef SND_DIFF
  esp::encodeDac8Diff(samples, staged, chunkSamples);
#else
  esp::encodeDac8Mono(samples, staged, chunkSamples);
#endif
}

// Output sits at 0 after boot. Move it to the idle level slowly: a jump clicks.
void rampToSilence() {
  const uint32_t entries = chunkSamples * CHANNELS;
  const uint32_t steps = RAMP_CHUNKS * entries;
  for (uint32_t chunk = 0; chunk < RAMP_CHUNKS; chunk++) {
    for (uint32_t i = 0; i < entries; i++) {
      staged[i] = (uint8_t)((esp::SILENCE >> 8) * (chunk * entries + i) / steps);
    }
    load(portMAX_DELAY);
  }
  AudioOut::flushSilence();
}

} // namespace

void AudioOut::init(const AudioConfig &cfg) {
  chunkSamples = cfg.chunk > MAX_CHUNK ? MAX_CHUNK : cfg.chunk;
  bufferCount = cfg.buffers;
#if CONFIG_IDF_TARGET_ESP32
  // ESP32 expands every 8 bit sample to a 16 bit word in the DMA buffer
  bufferBytes = chunkSamples * CHANNELS * 2;
#else
  bufferBytes = chunkSamples * CHANNELS;
#endif

  freeBuffers = xQueueCreate(bufferCount, sizeof(uint8_t *));

  dac_continuous_config_t dc = {};
#ifdef SND_DIFF
  dc.chan_mask = DAC_CHANNEL_MASK_ALL;
#elif defined(SND_LEFT_CHANNEL)
  dc.chan_mask = DAC_CHANNEL_MASK_CH1;  // GPIO26
#else
  dc.chan_mask = DAC_CHANNEL_MASK_CH0;  // GPIO25
#endif
  dc.desc_num = bufferCount;
  dc.buf_size = bufferBytes;
  dc.freq_hz = cfg.rate;
  dc.offset = 0;
  dc.clk_src = DAC_DIGI_CLK_SRC_APLL;
  // freq_hz is the I2S WS rate. ALTER = stereo FIFO: two words per WS, even
  // with one channel enabled (ESP32 dac_dma.c ignores chan_mask), so mono
  // would play every 2nd sample at twice the speed. SIMUL = one word per WS,
  // the old driver's I2S_CHANNEL_FMT_ONLY_RIGHT setup.
  dc.chan_mode = CHANNELS == 2 ? DAC_CHANNEL_MODE_ALTER : DAC_CHANNEL_MODE_SIMUL;
  dac_continuous_new_channels(&dc, &dac);

  dac_event_callbacks_t callbacks = {};
  callbacks.on_convert_done = onConvertDone;
  dac_continuous_register_event_callback(dac, &callbacks, nullptr);
  dac_continuous_enable(dac);
  dac_continuous_start_async_writing(dac);

  rampToSilence();
}

bool AudioOut::write(const int16_t *samples) {
  // whole chunks: a buffer is free or nothing is written
  stage(samples);
  return load(0);
}

void AudioOut::writeBlocking(const int16_t *samples) {
  stage(samples);
  load(portMAX_DELAY);
}

// Fill all DMA buffers with silence. Without writes (self-test, machine
// load) the DMA replays them in a loop: old content hums.
void AudioOut::flushSilence() {
  for (uint32_t i = 0; i < chunkSamples * CHANNELS; i++) {
    staged[i] = esp::SILENCE >> 8;
  }
  for (uint32_t i = 0; i < bufferCount; i++) {
    load(portMAX_DELAY);
  }
}

} // namespace hal

#else
#error "HAL_AUDIO_ESP_DAC: this chip has no DAC; set HAL_AUDIO_NULL or a codec backend"
#endif // SOC_DAC_SUPPORTED
#endif // HAL_AUDIO_ESP_DAC
