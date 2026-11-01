#include "audio.h"
#include <string.h>
#include "../machines/machineBase.h"
#include "../config.h"
#ifdef HAL_AUDIO_ES8311
#include <stdio.h>
#include "../audio/codec_es8311.h"

// codec output level, 0..100 (machines mix at full scale)
static const uint8_t ES8311_VOLUME = 80;
#endif

void Audio::init() {
  memset(pending, 0, sizeof(pending));
#ifdef HAL_AUDIO_ES8311
  // codec registers before the I2S stream starts; the controller shares the bus
  if (!hal::I2c::init(0, ES8311_I2C_SDA, ES8311_I2C_SCL, ES8311_I2C_CLK) ||
      !Es8311::init(0, ES8311_I2C_ADDR, SAMPLE_RATE, ES8311_VOLUME)) {
    printf("es8311: no codec at 0x%02x\n", ES8311_I2C_ADDR);
  }
#endif
  hal::AudioConfig cfg = {SAMPLE_RATE, AUDIO_CHUNK, DMA_BUF_COUNT};
  hal::AudioOut::init(cfg);
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
  memset(pending, 0, sizeof(pending));
  hal::AudioOut::flushSilence();
}

void Audio::transmit() {
  // (try to) transmit as much audio data as possible. The sink takes whole
  // chunks: either all or nothing is actually written.
  for (;;) {
    if (!hal::AudioOut::write(pending)) {
      return;
    }

    // stopped: pending stays silent
    if (!currentMachine) {
      continue;
    }

    // render the next audio chunk if data has actually been sent
    int16_t samples[AUDIO_CHUNK];
    currentMachine->renderAudio(samples, AUDIO_CHUNK);
    for (int i = 0; i < AUDIO_CHUNK; i++) {
      pending[i] = scaled(samples[i]);
    }
  }
}

int16_t Audio::scaled(int16_t value) const {
  if (muted) value = 0;

  // value is now in the range of +/- 512, so expand to +/- 15 bit
  value = value * 64;

  return value / volumeSetting;
}
