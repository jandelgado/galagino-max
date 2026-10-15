#ifndef AUDIO_H
#define AUDIO_H

#include <driver/i2s.h>
#include "../machines/machineBase.h"
#include "../config.h"
#include "ima_adpcm.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 4)
// See https://github.com/espressif/arduino-esp32/issues/8467
#define WORKAROUND_I2S_APLL_PROBLEM
// as a workaround we run dkong audio also at 24khz (instead of 11765)
// and fill the buffer at half the rate resulting in 12khz effective
// sample rate
#endif

#ifndef WORKAROUND_I2S_APLL_PROBLEM
// Machines render 24 kHz only (dkong doubles its 11765 Hz DAC itself).
#error "galagino audio needs ESP-IDF >= 4.4.4: fixed 24 kHz output"
#endif

class Audio {
public:
  void init();
  void start(machineBase *machineBase);
  void stop();
  void transmit();
  void volumeUpDown(bool up, bool down);
  void mute(int m);

private:
  // Idle output level. Built-in DAC is unsigned: 0x0000 = 0 V, 0x8000 =
  // VDD/2. Any other idle level steps the output on start/stop: click.
  static const uint16_t SILENCE = 0x8000;
  static const int DMA_BUF_COUNT = 4;
  // 16 x 64 samples = 43 ms at 24 kHz
  static const int RAMP_CHUNKS = 16;

  void flushSilence(void);
  void writeBlocking(void);
  void rampToSilence(void);
  void valueToBuffer(int index, short value);
  void phoenix_render_buffer(void);
  void vanguard_render_buffer(void);

  machineBase *currentMachine;
  signed char machineType;

#ifdef SND_DIFF
  unsigned short snd_buffer[128]; // buffer space for two channels
#else
  unsigned short snd_buffer[64];  // buffer space for a single channel
#endif

  short volumeSetting = 3;
  bool volumeUpLast;
  bool volumeDownLast;
  int  muted = 0;

  // ==========================================================================
  // Phoenix discrete audio — porting FEDELE da MAME src/mame/phoenix/phoenix_a.cpp
  // (phoenix_sound_device + phoenix_discrete netlist). Sostituisce la precedente
  // approssimazione "Galaxian-style" ereditata da SPINNERINO.
  // ==========================================================================

  // -- Noise generator (bit 6-7 latch sound A; NE555 + doppio inviluppo RC C24/C25) --
  int32_t  ph_c24_level = 0;   long ph_c24_counter = 0;
  int32_t  ph_c25_level = 0;   long ph_c25_counter = 0;
  uint32_t ph_noise_shiftreg = 0x1FFFF;   // LFSR 18-bit (EXNOR bit16/bit17)
  long     ph_noise_counter = 0;    int ph_noise_polybit = 0;
  long     ph_noise_lp_counter = 0; int ph_noise_lp_polybit = 0;

  // NB: tutto lo stato analogico e' in float (NON double): l'ESP32 ha la FPU solo
  // single-precision, i double sono emulati in software e a 24kHz sforano il budget
  // CPU del core (visto su HW: loopTask inchiodato in transmit() -> task watchdog).

  // -- Effect 1 (sound B: NODE_20 RCDISC4 -> NODE_21 555 CV -> NODE_22 NOTE -> NODE_25 filtro) --
  float    ph_e1_vc1 = 0;                          // NODE_20 (inviluppo pitch)
  float    ph_e1_555_cap = 0; uint8_t ph_e1_555_ff = 1;  // NODE_21
  int      ph_e1_note_c1 = 0, ph_e1_note_c2 = 0;   // NODE_22
  float    ph_e1_rcfilt = 0;                       // NODE_25

  // -- Effect 2 (sound A: NODE_33/34 555 -> mixer -> NODE_37 filtro lento -> NODE_39 555 CV -> NODE_40 NOTE) --
  float    ph_e2_555a_cap = 0; uint8_t ph_e2_555a_ff = 1;  // NODE_33
  float    ph_e2_555b_cap = 0; uint8_t ph_e2_555b_ff = 1;  // NODE_34
  float    ph_e2_rcfilt = 0;                               // NODE_37
  float    ph_e2_555cv_cap = 0; uint8_t ph_e2_555cv_ff = 1;// NODE_39
  int      ph_e2_note_c1 = 0, ph_e2_note_c2 = 0;           // NODE_40

  // -- Mixer finale (DISCRETE_MIXER4): cap di accoppiamento DC-block per canale + cAmp --
  float    ph_mix_vcap1 = 0, ph_mix_vcap2 = 0, ph_mix_vcamp = 0;

  // -- Melodia MM6221AA (4 tune brevi, approssimazione mantenuta da SPINNERINO) --
  uint8_t  ph_mel_idx  = 0;        // indice nota corrente
  uint8_t  ph_mel_tune = 0;        // 0..3 tune corrente
  uint16_t ph_mel_phase= 0;
  uint16_t ph_mel_freq = 0;
  uint16_t ph_mel_timer= 0;        // sample fino a prossima nota
  bool     ph_mel_active = false;

  // SNK6502 music (two channels in Vanguard, three in Fantasy) and effects.
  uint32_t vg_phase[3] = {0, 0, 0};
  uint16_t vg_offset[3] = {0, 0, 0};
  uint32_t vg_music_clock = 0;
  uint32_t vg_noise = 0x1ffff;
  uint8_t vg_last_port0 = 0;
  uint8_t vg_port_sequence[2] = {0, 0};
  uint8_t vg_music0_restart = 0;
  uint8_t vg_music1_restart = 0;
  uint8_t vg_music2_restart = 0;
  bool vg_music0_command_muted = true;
  bool vg_music1_command_muted = true;
  bool vg_music2_command_muted = true;
  bool vg_muted[3] = {true, true, true};
  uint32_t vg_sample_pos[3] = {0, 0, 0}; // fire, explosion, speech
  uint32_t vg_sample_len[3] = {0, 0, 0};
  const signed char *vg_sample_ptr[3] = {nullptr, nullptr, nullptr};
  int16_t vg_adpcm_predictor[3] = {0, 0, 0};
  int8_t vg_adpcm_step[3] = {0, 0, 0};
  uint8_t vg_sample_repeat[3] = {0, 0, 0}, vg_sample_divider[3] = {1, 1, 1};
  uint8_t vg_speech_sequence = 0;
};

#endif
