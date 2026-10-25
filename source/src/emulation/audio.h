#ifndef AUDIO_H
#define AUDIO_H

// Buffering, volume, mute. Each machine renders its own sound
// (machineBase::renderAudio, contract in audio_mix.h); the sink is hal::AudioOut.

#include <stdint.h>
#include "../config.h"
#include "../hal/hal.h"
#include "audio_mix.h"

class machineBase;

class Audio {
public:
  void init();
  void start(machineBase *machine);
  void stop();
  void transmit();
  void volumeUpDown(bool up, bool down);
  void mute(int m);

private:
  static const uint32_t SAMPLE_RATE = 24000;
  static const uint32_t DMA_BUF_COUNT = 4;

  int16_t scaled(int16_t value) const;

  machineBase *currentMachine = nullptr;

  // next chunk for the sink, volume applied; silence while no machine runs
  int16_t pending[AUDIO_CHUNK];

  short volumeSetting = 3;
  bool volumeUpLast;
  bool volumeDownLast;
  int  muted = 0;
};

#endif
