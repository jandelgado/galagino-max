#include "berzerk_audio.h"

static const uint32_t SPEECH_MASTER_HZ = 2500000; // 10 MHz / 4
static const uint16_t SPEECH_ROM_SIZE = 4096;

// board ports (offset from 0x40); all others go to the 6840
enum : uint8_t { PORT_SPEECH = 4, PORT_SFXCTRL = 6 };

// port 0x44 write, bits 7-6
enum : uint8_t { SPEECH_WORD = 0, SPEECH_CONTROL = 1 };
static const uint8_t SPEECH_NOT_BUSY = 0x40;

// MAME mix: speech 0.5 x gain / 7, effects 0.33, int16 full scale.
// (level -7..8) * gain * 293 = level / 8 * gain / 7 * 0.5 * 32767
static const int32_t SPEECH_SCALE = 293;
static const int32_t SFX_MUL = 21, SFX_SHIFT = 6; // 21 / 64 = 0.33

void BerzerkAudio::reset(const uint8_t *speech_rom) {
  speech.reset(speech_rom, SPEECH_ROM_SIZE);
  sfx.reset(OUT_HZ);
  ring.reset(OUT_HZ, OUT_HZ);
  line_acc = speech_frac = 0;
  speech_level = 0;
  write(PORT_SPEECH, SPEECH_CONTROL << 6); // MAME sound_reset: gain 0, slowest clock
}

void BerzerkAudio::write(uint8_t offset, uint8_t data) {
  if (offset == PORT_SFXCTRL) {
    sfx.sfxctrl_w(data >> 6, data);
    return;
  }
  if (offset != PORT_SPEECH) {
    sfx.ptm_w(offset, data);
    return;
  }

  if ((data >> 6) == SPEECH_WORD) {
    speech.data_w(data & 0x3f);
    speech.start();
  } else if ((data >> 6) == SPEECH_CONTROL) {
    // clock: 2.5 MHz / (9..16) / 8 = 19.5 .. 34.7 kHz
    speech_gain = (data >> 3) & 7;
    const uint32_t hz = SPEECH_MASTER_HZ / (16 - (data & 7)) / 8;
    speech_step = (hz << 16) / OUT_HZ;
  }
}

uint8_t BerzerkAudio::read(uint8_t offset) {
  if (offset == PORT_SPEECH) {
    return speech.busy() ? 0x00 : SPEECH_NOT_BUSY;
  }
  if (offset == PORT_SFXCTRL) {
    return 0; // open bus
  }
  return sfx.ptm_r(offset);
}

// one OUT_HZ sample: chip clocks due in this sample averaged, plus effects
void BerzerkAudio::emit() {
  speech_frac += speech_step;
  const uint32_t n = speech_frac >> 16;
  speech_frac &= 0xffff;
  if (n) {
    int32_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
      sum += (int32_t)speech.clock() - 7;
    }
    speech_level = sum / (int32_t)n;
  }

  const int32_t v = speech_level * speech_gain * SPEECH_SCALE + ((sfx.sample() * SFX_MUL) >> SFX_SHIFT);
  ring.push((int16_t)(v > INT16_MAX ? INT16_MAX : (v < INT16_MIN ? INT16_MIN : v)));
}

void BerzerkAudio::line() {
  line_acc += OUT_HZ;
  while (line_acc >= LINE_HZ) {
    line_acc -= LINE_HZ;
    emit();
  }
}
