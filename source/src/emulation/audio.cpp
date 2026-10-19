#include "audio.h"
#include <string.h>
#include "../machines/machineBase.h"
#ifdef ES8311_AUDIO
#include <es8311.h>
#endif

void Audio::init() {
#ifdef ES8311_AUDIO
  bool isInit = i2cIsInit(0);
  if (isInit) {
    printf("es8311: i2c Clock=%d\n", Wire.getClock());
  }
  else {
    bool done = Wire.begin(ES8311_I2C_SDA, ES8311_I2C_SCL, ES8311_I2C_CLK);
    printf("es8311: i2c_init=%d SDA=%d SCL=%d Clock=%d\n",
      done, ES8311_I2C_SDA, ES8311_I2C_SCL,
      Wire.getClock());
    if (!done) {
      printf("es8311: i2c failed\n");
      return;
    }
  }

  // check we can talk to es8311
  Wire.beginTransmission((uint8_t)ES8311_I2C_ADDR);
  int8_t err = Wire.endTransmission();

  printf("es8311: addr=0x%02x err=%d\n", ES8311_I2C_ADDR, err);

  if (err != 0) {
    printf("es8311: can't find es8311 in i2c bus at addr=0%02x err=%d\n", ES8311_I2C_ADDR, err);
  }

  es8311_handle_t es8311_h = es8311_create(0, ES8311_I2C_ADDR);
  es8311_clock_config_t es8311_clock = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = 24000 * 256,
    .sample_frequency = 24000
  };

  esp_err_t es_err;
  es_err = es8311_init(es8311_h, &es8311_clock, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
  printf("es8311: es8311_init err=%d\n", es_err);

  es_err = es8311_sample_frequency_config(es8311_h, 24000 * 256, 24000);
  printf("es8311: es8311_sample_frequency_config err=%d\n", es_err);

  es_err = es8311_voice_volume_set(es8311_h, 80, NULL);
  printf("es8311: es8311_voice_volume_set err=%d\n", es_err);

  const i2s_pin_config_t pin_config = {
    .mck_io_num = ES8311_I2S_MCK,
    .bck_io_num = ES8311_I2S_BCK,
    .ws_io_num = ES8311_I2S_WS,
    .data_out_num = ES8311_I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE,
  };

  const i2s_config_t i2s_config = {
    .mode =                 (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate =          24000,
    .bits_per_sample =      I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format =       I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_MSB,
    .intr_alloc_flags = 0,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = 64,
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
    .sample_rate = 24000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
#ifdef SND_DIFF
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
#elif defined(SND_LEFT_CHANNEL) // For devices using the left channel (e.g. CYD)
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
#else
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
#endif
    .intr_alloc_flags = 0,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = 64,   // 64 samples
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

// Fill all DMA buffers with silence. Without transmit() calls (self-test,
// machine load) the DMA replays them in a loop: old content hums.
void Audio::flushSilence(void) {
  for (uint16_t i = 0; i < sizeof(snd_buffer) / sizeof(snd_buffer[0]); i++) {
    snd_buffer[i] = SILENCE;
  }

  for (int i = 0; i < DMA_BUF_COUNT; i++) {
    writeBlocking();
  }
}

// Queue snd_buffer once, waiting for a free DMA buffer.
void Audio::writeBlocking(void) {
  size_t bytesOut = 0;
  i2s_write(I2S_NUM_0, snd_buffer, sizeof(snd_buffer), &bytesOut, portMAX_DELAY);
}

// Output sits at 0 after boot. Move it to SILENCE slowly: a jump clicks.
void Audio::rampToSilence(void) {
  const uint32_t entries = sizeof(snd_buffer) / sizeof(snd_buffer[0]);
  const uint32_t steps = RAMP_CHUNKS * entries;

  for (uint32_t chunk = 0; chunk < RAMP_CHUNKS; chunk++) {
    for (uint32_t i = 0; i < entries; i++) {
      const uint32_t level = SILENCE * (chunk * entries + i) / steps;
#ifdef ES8311_AUDIO
      // signed codec: SILENCE is -32768, ramp 0 down to it
      snd_buffer[i] = (uint16_t)(0x10000 - level);
#else
      snd_buffer[i] = (uint16_t)level;
#endif
    }
    writeBlocking();
  }

  flushSilence();
}

void Audio::start(machineBase *machine) {
  currentMachine = machine;
}

void Audio::volumeUpDown(bool up, bool down) {
  auto entry = volumeSetting;

  if (up && !volumeUpLast) {
    if (volumeSetting > 1)
      volumeSetting--;
  }
  volumeUpLast = up;

  if (down && !volumeDownLast) {
    if (volumeSetting < 30)
      volumeSetting++;
  }
  volumeDownLast = down;

  if (entry != volumeSetting)
    printf("VolumeUpDown: %d\n", volumeSetting);
}

void Audio::mute(int m) {
  muted = m;
}

// Machine is deleted after this; stop reading its registers.
void Audio::stop() {
  currentMachine = nullptr;
  flushSilence();
}

void Audio::transmit() {
  // (try to) transmit as much audio data as possible. Since we
  // write data in exact the size of the DMA buffers we can be sure
  // that either all or nothing is actually being written
  size_t bytesOut = 0;
  do {
    i2s_write(I2S_NUM_0, snd_buffer, sizeof(snd_buffer), &bytesOut, 0);
    if (!bytesOut) {
      return;
    }

    // stopped: buffer stays silent
    if (!currentMachine) {
      continue;
    }

    // render the next audio chunk if data has actually been sent
    int16_t samples[AUDIO_CHUNK];
    currentMachine->renderAudio(samples, AUDIO_CHUNK);
    for (int i = 0; i < AUDIO_CHUNK; i++) {
      valueToBuffer(i, samples[i]);
    }
  } while (bytesOut);
}

void Audio::valueToBuffer(int index, short value) {
  if (muted) value = 0;

  // value is now in the range of +/- 512, so expand to +/- 15 bit
  value = value * 64;

#ifdef SND_DIFF
  // generate differential output
  snd_buffer[2 * index]   = SILENCE + (value / volumeSetting);    // positive signal on GPIO26
  snd_buffer[2 * index + 1] = SILENCE - (value / volumeSetting);    // negative signal on GPIO25
#else
  // work-around weird byte order bug, see
  // https://github.com/espressif/arduino-esp32/issues/8467#issuecomment-1656616015
  snd_buffer[index ^ 1]   = SILENCE + (value / volumeSetting);
#endif
}

