#include "audio.h"
#include <math.h>
#include <string.h>
#ifdef ES8311_AUDIO
#include <es8311.h>
#endif

#include "ima_adpcm.h"
#include "dc_block.h"
using ima_adpcm::ima_adpcm_decode;

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

void Audio::start(machineBase *machineBase) {
  currentMachine = machineBase;
  machineType = currentMachine->machineType();

  // Phoenix
  ph_c24_level = ph_c25_level = 0;
  ph_c24_counter = ph_c25_counter = 0;
  ph_noise_shiftreg = 0x1FFFF;
  ph_noise_counter = ph_noise_lp_counter = 0;
  ph_noise_polybit = ph_noise_lp_polybit = 0;
  ph_e1_vc1 = 0;
  ph_e1_555_cap = 0;
  ph_e1_555_ff = 1;
  ph_e1_note_c1 = ph_e1_note_c2 = 0;
  ph_e2_555a_cap = ph_e2_555b_cap = ph_e2_rcfilt = ph_e2_555cv_cap = 0;
  ph_e2_555a_ff = ph_e2_555b_ff = ph_e2_555cv_ff = 1;
  ph_e2_note_c1 = ph_e2_note_c2 = 0;
  ph_mix_vcap1 = ph_mix_vcap2 = ph_mix_vcamp = 0;
  ph_mel_idx  = ph_mel_tune = 0;
  ph_mel_phase = ph_mel_freq = ph_mel_timer = 0;
  ph_mel_active = false;

  vg_phase[0] = vg_phase[1] = vg_phase[2] = 0;
  vg_offset[0] = vg_offset[1] = vg_offset[2] = 0;
  vg_music_clock = 0;
  vg_noise = 0x1ffff;
  vg_last_port0 = 0;
  vg_port_sequence[0] = currentMachine->soundregs[5];
  vg_port_sequence[1] = currentMachine->soundregs[6];
  vg_music0_restart = currentMachine->soundregs[8];
  vg_music1_restart = currentMachine->soundregs[9];
  vg_music2_restart = currentMachine->soundregs[11];
  vg_music0_command_muted = currentMachine->vanguardMusic0Muted();
  vg_music1_command_muted = currentMachine->vanguardMusic1Muted();
  vg_music2_command_muted = currentMachine->vanguardMusic2Muted();
  vg_muted[0] = vg_music0_command_muted;
  vg_muted[1] = vg_music1_command_muted;
  vg_muted[2] = vg_music2_command_muted;
  for (int i=0;i<3;i++) {
    vg_sample_pos[i]=vg_sample_len[i]=0;vg_sample_ptr[i]=nullptr;
    vg_adpcm_predictor[i]=vg_adpcm_step[i]=vg_sample_repeat[i]=0;vg_sample_divider[i]=1;
  }
  vg_speech_sequence = currentMachine->soundregs[4];
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
    // copy data in i2s dma buffer if possible
    i2s_write(I2S_NUM_0, snd_buffer, sizeof(snd_buffer), &bytesOut, 0);
    if (!bytesOut)
      return;

    // menu: no machine, buffer stays silent
    if (!currentMachine)
      continue;

    // render the next audio chunk if data has actually been sent
    if (machineType == MCH_PHOENIX && currentMachine->game_started)
      phoenix_render_buffer();
    else if(machineType == MCH_VANGUARD || machineType == MCH_FANTASY || machineType == MCH_NIBBLER)
      vanguard_render_buffer();
    else {
      int16_t samples[AUDIO_CHUNK];
      currentMachine->renderAudio(samples, AUDIO_CHUNK);
      for (int i = 0; i < AUDIO_CHUNK; i++) {
        valueToBuffer(i, samples[i]);
      }
    }
  } while(bytesOut);
}

void Audio::vanguard_render_buffer(void) {
  const bool is_fantasy = machineType == MCH_FANTASY || machineType == MCH_NIBBLER;
  const uint8_t p0 = currentMachine->soundregs[0];
  const uint8_t p1 = currentMachine->soundregs[1];
  const uint8_t shapes = currentMachine->soundregs[2];
  const uint8_t p3 = currentMachine->soundregs[10];

  // These controls are evaluated on a CPU write, not continuously from the
  // latched level. Continuously resetting offset 0 made the opening tune stick.
  if(currentMachine->soundregs[5]!=vg_port_sequence[0]){
    vg_port_sequence[0]=currentMachine->soundregs[5];
    if (!is_fantasy && (p0 & 0x20) && !(vg_last_port0 & 0x20)) {
      vg_sample_ptr[0]=currentMachine->vanguardSample(0);
      vg_sample_len[0]=currentMachine->vanguardSampleLength(0);vg_sample_pos[0]=0;
      vg_sample_divider[0]=currentMachine->vanguardSampleDivider(0);vg_sample_repeat[0]=0;
    } else if (!(p0&0x20) && (vg_last_port0&0x20)) vg_sample_len[0]=0;
    if (!is_fantasy && (p0 & 0x80) && !(vg_last_port0 & 0x80)) {
      vg_sample_ptr[1]=currentMachine->vanguardSample(1);
      vg_sample_len[1]=currentMachine->vanguardSampleLength(1);vg_sample_pos[1]=0;
      vg_sample_divider[1]=currentMachine->vanguardSampleDivider(1);vg_sample_repeat[1]=0;
    }
    vg_last_port0=p0;
  }
  if(currentMachine->soundregs[8]!=vg_music0_restart){
    vg_music0_restart=currentMachine->soundregs[8];vg_offset[0]=0;vg_muted[0]=false;
  }
  bool command_muted=currentMachine->vanguardMusic0Muted();
  if(command_muted!=vg_music0_command_muted){
    vg_music0_command_muted=command_muted;
    if(command_muted){vg_muted[0]=true;vg_offset[0]=0;}
    else {vg_muted[0]=false;vg_offset[0]=0;}
  }
  vg_port_sequence[1]=currentMachine->soundregs[6];
  if(currentMachine->soundregs[9]!=vg_music1_restart){
    vg_music1_restart=currentMachine->soundregs[9];vg_offset[1]=0;vg_muted[1]=false;
  }
  bool command_muted1=currentMachine->vanguardMusic1Muted();
  if(command_muted1!=vg_music1_command_muted){
    vg_music1_command_muted=command_muted1;
    if(command_muted1){vg_muted[1]=true;vg_offset[1]=0;}
    else {vg_muted[1]=false;vg_offset[1]=0;}
  }
  if(currentMachine->soundregs[11]!=vg_music2_restart){
    vg_music2_restart=currentMachine->soundregs[11];vg_offset[2]=0;vg_muted[2]=false;
  }
  bool command_muted2=currentMachine->vanguardMusic2Muted();
  if(command_muted2!=vg_music2_command_muted){
    vg_music2_command_muted=command_muted2;
    if(command_muted2){vg_muted[2]=true;vg_offset[2]=0;}
    else {vg_muted[2]=false;vg_offset[2]=0;}
  }
  uint8_t speech_sequence=currentMachine->soundregs[4];
  if(speech_sequence!=vg_speech_sequence){
    vg_speech_sequence=speech_sequence;
    uint8_t phrase=currentMachine->soundregs[3];
    if(phrase==0xff)vg_sample_len[2]=0;
    else if(vg_sample_pos[2]>=vg_sample_len[2]){
      vg_sample_ptr[2]=currentMachine->vanguardSample(2+phrase);
      vg_sample_len[2]=currentMachine->vanguardSampleLength(2+phrase);vg_sample_pos[2]=0;
      vg_sample_divider[2]=currentMachine->vanguardSampleDivider(2+phrase);vg_sample_repeat[2]=0;
    }
  }

  // Exact resistor-weighted waveform construction used by the SNK circuit.
  int8_t form[3][16];
  for(int ch=0;ch<(is_fantasy?3:2);ch++){
    if(ch==2){
      for(int pos=0;pos<16;pos++)form[ch][pos]=(pos&8)?7:-8;
      continue;
    }
    uint8_t mask=ch?(shapes>>4):(is_fantasy ?
      ((shapes&9)|((shapes&2)<<1)|((shapes&4)>>1)) :
      ((shapes&3)|((shapes&4)<<1)|((shapes&8)>>1)));
    int bit3=(mask&3)?8:((mask&4)?4:((mask&8)?2:0));
    int bit2=(mask&4)?8:((mask&0x0a)?4:0);
    int bit1=(mask&8)?8:((mask&4)?4:((mask&2)?2:0));
    int bit0=bit1>>1;
    if(bit0+bit1+bit2+bit3<16){bit0<<=1;bit1<<=1;bit2<<=1;bit3<<=1;}
    int base=(bit0+bit1+bit2+bit3+1)>>1;
    for(int pos=0;pos<16;pos++)
      form[ch][pos]=((pos&1)?bit0:0)+((pos&2)?bit1:0)+
                    ((pos&4)?bit2:0)+((pos&8)?bit3:0)-base;
  }

  for (int i = 0; i < 64; i++) {
    int sample = 0;
    for (int ch = 0; ch < (is_fantasy ? 3 : 2); ch++) {
      if (vg_muted[ch]) continue;
      unsigned short base;
      if(ch==2)base=0x1000|((p3&0x70)<<4);
      else base=(ch ? 0x0800 : 0)|((ch ? p1 : p0)&7)*0x100;
      uint8_t note = currentMachine->vanguardSoundRom(base + vg_offset[ch]);
      if (note == 0xff) continue;
      uint32_t step = 939349UL / (256 - note); // 43000*8/24000 in Q16
      uint32_t next_phase = vg_phase[ch] + step;
      int previous = form[ch][(vg_phase[ch] >> 16) & 15];
      int current = form[ch][(next_phase >> 16) & 15];
      uint32_t fraction = next_phase & 0xffff;
      sample += ((previous * (int)(0x10000 - fraction) +
                  current * (int)fraction) >> 16) * 16;
      vg_phase[ch] = next_phase;
    }

    // IMA-ADPCM samples: effects at 8 kHz, speech at 6 kHz, held to 24 kHz.
    for(int voice=0;voice<3;voice++)if(vg_sample_ptr[voice] && vg_sample_pos[voice]<vg_sample_len[voice]){
      if(!vg_sample_repeat[voice]){
        if(!vg_sample_pos[voice]){
          const uint8_t *encoded=(const uint8_t *)vg_sample_ptr[voice];
          vg_adpcm_predictor[voice]=(int16_t)(encoded[0]|(encoded[1]<<8));vg_adpcm_step[voice]=0;
        }else{
          uint32_t nibble=vg_sample_pos[voice]-1;
          uint8_t packed=(uint8_t)vg_sample_ptr[voice][2+(nibble>>1)];
          uint8_t code=(nibble&1)?(packed>>4):(packed&15);
          ima_adpcm_decode(vg_adpcm_predictor[voice], vg_adpcm_step[voice], code);
        }
        vg_sample_pos[voice]++;vg_sample_repeat[voice]=vg_sample_divider[voice];
      }
      sample+=(vg_adpcm_predictor[voice]>>8);vg_sample_repeat[voice]--;
    }

    // Shot B is the cabinet's second SN76477 and remains synthesized noise.
    uint32_t feedback = (vg_noise ^ (vg_noise >> 3)) & 1;
    vg_noise = (vg_noise >> 1) | (feedback << 16);
    int noise = (vg_noise & 1) ? 1 : -1;
    if (!is_fantasy && (p0 & 0x40)) sample += noise * 70;
    if (is_fantasy && (p0 & 0x80)) sample += noise * 55;

    vg_music_clock += is_fantasy ? 416 : 3899;
    uint32_t music_threshold=is_fantasy ? 240000 : 2400000;
    if (vg_music_clock >= music_threshold) {
      vg_music_clock -= music_threshold;
      for (int ch=0;ch<(is_fantasy?3:2);ch++) vg_offset[ch]=(vg_offset[ch]+1)&0xff;
      if (!is_fantasy && vg_offset[0] == 0) {
        vg_muted[0]=true;
        vg_music0_command_muted=true;
        currentMachine->vanguardMusic0Ended();
      }
    }
    if (sample > 500) sample = 500; else if (sample < -500) sample = -500;
    valueToBuffer(i, sample);
  }
}

// ============================================================================
// PHOENIX discrete audio @ 24 kHz mono — porting FEDELE da MAME
// src/mame/phoenix/phoenix_a.cpp (phoenix_sound_device + phoenix_discrete netlist).
// Ogni stadio replica la fisica RC/555 esatta del circuito reale (non uno square
// wave approssimato). Sostituisce la precedente approssimazione "Galaxian-style".
//
// Mappa registri (raw latch, settati da Phoenix::wrZ80):
//   soundregs[0] = sound A (0x6000): bit0-3=PHOENIX_EFFECT_2_DATA, bit4-5=EFFECT_2_FREQ,
//                  bit6-7=noise gen (C24/C25 charge/discharge, "future effect 3/4")
//   soundregs[1] = sound B (0x6800): bit0-3=PHOENIX_EFFECT_1_DATA, bit4=EFFECT_1_FREQ,
//                  bit5=EFFECT_1_FILT, bit6-7=melody tune select (MM6221AA)
//
// Catena Effect 1 (comment originale: "shield, bird explode, level 3&4 siren,
// level 5 spaceship"): NODE_20 RCDISC4 (inviluppo pitch pilotato da FREQ bit) ->
// NODE_21 555 CV -> NODE_22 NOTE (LS163+preload DATA) -> switch/filtro (FILT bit).
//
// Catena Effect 2 (comment originale: "bird flying, bird/phoenix/spaceship hit,
// phoenix wing hit"): NODE_30 selezione capacita' (FREQ 2-bit) -> NODE_33/34 due
// 555 fissi -> mixer resistivo -> NODE_37 filtro lento (~0.3s) -> NODE_38 mixer ->
// NODE_39 555 CV -> NODE_40 NOTE (preload DATA) -> * livello ampiezza (FREQ bit alto).
//
// Rumore (bit 6-7 sound A, MAI usato dall'approssimazione precedente): NE555 con
// doppio inviluppo RC (C24 ~0.136s discharge, C25 ~0.32s discharge) che modula la
// frequenza di un LFSR 18-bit tra 588-6325 Hz (porting diretto di noise()/update_c24/
// update_c25 da phoenix_a.cpp, invariato).
//
// Melody (MM6221AA, bit 6-7 sound B): approssimazione mantenuta da SPINNERINO (4 tune
// brevi, chip melodia reale non disponibile come sorgente separato).
//
// NOTE: alcune costanti MAME non verificabili da qui (DEFAULT_TTL_V_LOGIC_1=3.4,
// OP_AMP_VP_RAIL_OFFSET=1.5) sono valori standard assunti: influenzano solo il
// bilanciamento fine dei livelli, non la frequenza/tempistica (che e' esatta).
// Guadagno finale PHOENIX_MASTER_SCALE tarabile se il volume risultasse squilibrato.
//
// PERFORMANCE (fix task watchdog su HW): tutta la catena e' in float con gli
// esponenziali RC del passo pieno PRECALCOLATI (le combinazioni R/C sono note a
// priori). L'ESP32 ha la FPU solo single-precision: la prima stesura in double
// (exp/log software, ~migliaia di cicli l'una, ~10 per sample a 24kHz) superava
// il budget dell'intero core -> transmit() non riempiva mai il DMA piu' veloce
// dell'I2S, loopTask restava nel do/while e scattava il task watchdog (reboot).
// exp/log runtime sopravvivono solo nella correzione sub-sample dei crossing
// (~1-3 per sample, ph_neg_log1m()) e nel raro fallback di ph_exp1m.
// ============================================================================

// Parametri precalcolati di un 555 astabile (R/C fissi per ogni istanza).
struct ph555Params {
  float exp_charge;        // 1-exp(-dt/tau) per il passo pieno da 1/24000s
  float exp_discharge;
  float tau_charge;        // (r1+r2)*c
  float tau_discharge;     // r2*c
  float inv_tau_charge;
  float inv_tau_discharge;
};

static ph555Params ph_make_555(float r1, float r2, float c) {
  const float dt = 1.0f / 24000.0f;
  ph555Params p;
  p.tau_charge        = (r1 + r2) * c;
  p.tau_discharge     = r2 * c;
  p.inv_tau_charge    = 1.0f / p.tau_charge;
  p.inv_tau_discharge = 1.0f / p.tau_discharge;
  p.exp_charge        = 1.0f - expf(-dt * p.inv_tau_charge);
  p.exp_discharge     = 1.0f - expf(-dt * p.inv_tau_discharge);
  return p;
}

// 1-exp(-x): serie troncata al 5° ordine per x<=1 (errore <0.1%), usata per il
// tempo residuo dopo un crossing (dt < passo pieno). Fallback expf per x>1
// (solo il 555 CV di Effect2 in scarica puo' arrivarci, tau < dt_full).
static inline float ph_exp1m(float x) {
  if (x > 1.0f) return 1.0f - expf(-x);
  return x * (1.0f - 0.5f*x*(1.0f - 0.333333f*x*(1.0f - 0.25f*x*(1.0f - 0.2f*x))));
}

// -log(1-f) for the crossing time of the 555s below, f in [0, 0.999].
// They cross on most samples, newlib logf() there cost more than the rest
// of the netlist. ln(x) = e*ln2 + 2*atanh((m-1)/(m+1)), m in [1,2):
// absolute error < 2e-6.
static inline float ph_neg_log1m(float f) {
  union { float f; uint32_t i; } u = { 1.0f - f };
  const int e = (int)((u.i >> 23) & 0xff) - 127;
  u.i = (u.i & 0x007fffff) | 0x3f800000;
  const float t = (u.f - 1.0f) / (u.f + 1.0f);
  const float t2 = t * t;
  const float ln_m = 2.0f * t * (1.0f + t2 * (1.0f/3 + t2 * (1.0f/5 + t2 * (1.0f/7 + t2 * (1.0f/9)))));
  return -(e * 0.69314718f + ln_m);
}

// -- 555 astabile (dsd_555_astbl): RC charge/discharge esatto con correzione
// dell'overshoot in tempo continuo. ctrlv<0 => soglie fisse (v_pos*2/3, v_pos/3);
// ctrlv>=0 => CV-modulato (threshold=ctrlv, trigger=ctrlv/2).
// energy_mode=false -> ritorna count_f+x_time (DISC_555_OUT_COUNT_F_X, per NOTE);
// energy_mode=true  -> ritorna v_out_high*(duty) (DISC_555_OUT_ENERGY, per i mixer).
static float ph_555_step(float &cap_v, uint8_t &ff, const ph555Params &p,
                         float v_pos, float v_charge, float ctrlv,
                         bool energy_mode, float v_out_high) {
  const float dt_full = 1.0f / 24000.0f;
  if (ctrlv >= 0 && ctrlv < 0.25f)
    return energy_mode ? (ff ? 0.0f : v_out_high) : 0.0f;

  float threshold = (ctrlv >= 0) ? ctrlv          : (v_pos * (2.0f / 3.0f));
  float trigger   = (ctrlv >= 0) ? (0.5f * ctrlv) : (v_pos * (1.0f / 3.0f));
  int count_f = 0;

  if (ctrlv >= 0) {
    if (cap_v >= threshold)      { ff = 0; count_f++; }
    else if (cap_v <= trigger)   { ff = 1; }
  }

  float dt     = dt_full;
  float v_cap  = cap_v;
  float x_time = 0;
  bool  full_step = true;   // primo giro: esponente precalcolato del passo pieno
  int   guard = 8;          // rete di sicurezza: mai piu' di 8 crossing per sample

  do {
    if (ff) {
      float exponent   = full_step ? p.exp_charge : ph_exp1m(dt * p.inv_tau_charge);
      float v_cap_next = v_cap + (v_charge - v_cap) * exponent;
      dt = 0;
      if (v_cap_next >= threshold) {
        float denom = v_charge - v_cap;
        if (denom > 1e-9f) {
          float f = (v_cap_next - threshold) / denom;
          if (f > 0.999f) f = 0.999f;
          dt = p.tau_charge * ph_neg_log1m(f);
        }
        x_time = dt;
        v_cap_next = threshold;
        ff = 0;
        count_f++;
      }
      v_cap = v_cap_next;
    } else {
      float exponent   = full_step ? p.exp_discharge : ph_exp1m(dt * p.inv_tau_discharge);
      float v_cap_next = v_cap - v_cap * exponent;
      dt = 0;
      if (v_cap_next <= trigger) {
        if (v_cap_next < trigger && v_cap > 1e-9f) {
          float f = (trigger - v_cap_next) / v_cap;
          if (f > 0.999f) f = 0.999f;
          dt = p.tau_discharge * ph_neg_log1m(f);
        }
        x_time = dt;
        v_cap_next = trigger;
        ff = 1;
      }
      v_cap = v_cap_next;
    }
    full_step = false;
  } while (dt > 0 && --guard);

  cap_v = v_cap;
  float x_frac = x_time * 24000.0f;   // = x_time / dt_full

  if (energy_mode) {
    float xt = (x_frac == 0) ? 1.0f : x_frac;
    return v_out_high * (ff ? xt : (1.0f - xt));
  }
  return count_f ? (count_f + x_frac) : 0.0f;
}

// -- NOTE generator (dss_note, DISC_CLK_BY_COUNT | DISC_OUT_IS_ENERGY): contatore
// LS163 che conta da 'data' (preload) a 15 poi ribalta count2 (0/1) -- la nota
// suonata e' il toggle di count2. clock_in = output combinato del 555 (count_f+x_time).
static float ph_note_step(int &count1, int &count2, float clock_in, int data) {
  int   clock  = (int)clock_in;
  float x_time = clock_in - clock;
  int   last_count2 = count2;

  if (data != 15) {
    for (int k = 0; k < clock; k++) {
      count1++;
      if (count1 > 15) { count1 = data; count2 += 1; if (count2 > 1) count2 = 0; }
    }
  }

  float v_out = count2;
  if (count2 != last_count2) {
    if (x_time == 0) x_time = 1.0f;
    v_out = last_count2;
    if (count2 > last_count2) v_out += (count2 - last_count2) * x_time;
    else                      v_out -= (last_count2 - count2) * x_time;
  }
  return v_out;
}

// -- RCDISC4 tipo 1 (NODE_20): inviluppo di tensione per il pitch-bend di Effect 1,
// pilotato dal bit FREQ. Costanti precalcolate da R22=470,R23=100k,R24=33k,C7=6.8uF,VP=12V.
static float ph_rcdisc4_step(float &vc1, int freq_bit) {
  static const float dt   = 1.0f / 24000.0f;
  static const float V    = 12.0f - 0.5f;                                    // VP - diode drop
  static const float r1p3 = (470.0f * 33000.0f) / (470.0f + 33000.0f);       // R1||R3
  static const float rT1  = 100000.0f + r1p3;                                // R2+r
  static const float m_v1 = (V / rT1) * r1p3 + 0.5f;
  static const float rT1b = (100000.0f * r1p3) / (100000.0f + r1p3);         // R2||r
  static const float exp1 = 1.0f - expf(-dt / (rT1b * 6.8e-6f));
  static const float rT0  = 100000.0f + 33000.0f;                            // R2+R3
  static const float m_v0 = (V / rT0) * 33000.0f + 0.5f;
  static const float rT0b = (100000.0f * 33000.0f) / (100000.0f + 33000.0f); // R2||R3
  static const float exp0 = 1.0f - expf(-dt / (rT0b * 6.8e-6f));

  float target   = freq_bit ? m_v1 : m_v0;
  float exponent = freq_bit ? exp1 : exp0;
  vc1 += (target - vc1) * exponent;
  if (vc1 > 10.5f) vc1 = 10.5f;   // max_out = VP(12) - OP_AMP_VP_RAIL_OFFSET(1.5)
  if (vc1 < 0)     vc1 = 0;
  return vc1;
}

// -- RC filter a un polo (dst_rcfilter): v += (vin-v)*exponent, exponent precalcolato dal chiamante.
static inline float ph_rcfilter_step(float &v_out, float vin, float exponent) {
  v_out += (vin - v_out) * exponent;
  return v_out;
}

// -- Noise generator NE555 + doppio inviluppo RC (porting diretto di update_c24/
// update_c25/noise da phoenix_a.cpp, invariato). Pilotato da bit 6/7 del sound A latch.
// Rates as float reciprocals: double math is software on the ESP32, two
// double divisions per sample cost more than the rest of the netlist.
static int32_t ph_update_c24(int32_t &level, long &counter, bool bit40) {
  static const float C24 = 6.8e-6f, R49 = 1000, R51 = 330, R52 = 20000;
  static const float RATE_DOWN = 1.0f / (R52 * C24);
  static const float RATE_UP = 1.0f / ((R51 + R49) * C24);
  if (bit40) {
    if (level > 0) {
      counter -= (long)((level - 0) * RATE_DOWN);
      if (counter <= 0) {
        long n = -counter / 24000 + 1;
        counter += n * 24000;
        level -= n; if (level < 0) level = 0;
      }
    }
  } else {
    if (level < 32767) {
      counter -= (long)((32767 - level) * RATE_UP);
      if (counter <= 0) {
        long n = -counter / 24000 + 1;
        counter += n * 24000;
        level += n; if (level > 32767) level = 32767;
      }
    }
  }
  return 32767 - level;
}

static int32_t ph_update_c25(int32_t &level, long &counter, bool bit80) {
  static const float C25 = 6.8e-6f, R50 = 1000, R53 = 330, R54 = 47000;
  static const float RATE_UP = 1.0f / ((R50 + R53) * C25);
  static const float RATE_DOWN = 1.0f / (R54 * C25);
  if (bit80) {
    if (level < 32767) {
      counter -= (long)((32767 - level) * RATE_UP);
      if (counter <= 0) {
        long n = -counter / 24000 + 1;
        counter += n * 24000;
        level += n; if (level > 32767) level = 32767;
      }
    }
  } else {
    if (level > 0) {
      counter -= (long)((level - 0) * RATE_DOWN);
      if (counter <= 0) {
        long n = -counter / 24000 + 1;
        counter += n * 24000;
        level -= n; if (level < 0) level = 0;
      }
    }
  }
  return level;
}

void Audio::phoenix_render_buffer(void) {
  static const uint16_t TUNE_ROMANCE[] = {
    440, 0, 440, 0, 440, 0,        // A4 A4 A4
    440, 0, 392, 0, 349, 0,        // A4 G4 F4
    349, 0, 330, 0, 294, 0,        // F4 E4 D4
    294, 0, 349, 0, 440, 0,        // D4 F4 A4
    587, 0,   0, 0,   0, 0,        // D5 [sustain lungo]
    587, 0, 523, 0, 466, 0,        // D5 C5 Bb4
    466, 0, 440, 0, 392, 0,        // Bb4 A4 G4
    392, 0, 440, 0, 466, 0,        // G4 A4 Bb4
    440, 0, 466, 0, 440, 0,        // A4 Bb4 A4
    554, 0, 466, 0, 440, 0,        // C#5 Bb4 A4
    440, 0, 392, 0, 349, 0,        // A4 G4 F4
    349, 0, 330, 0, 294, 0,        // F4 E4 D4
    330, 0, 330, 0, 330, 0,        // E4 E4 E4
    330, 0, 349, 0, 330, 0,        // E4 F4 E4
    294, 0, 349, 0, 440, 0,        // D4 F4 A4
    587, 0,   0, 0,   0, 0,        // D5 [sustain finale]
    0xFFFF
  };
  // TUNE_ROMANCE dura 96 voci * 0.2s = 19.2s e (confermato dall'utente) suona per
  // intero senza tagli al livello 1 — corretto per confronto con MAME. FUR_ELISE e
  // WARNING erano invece placeholder brevi (~1.6-1.8s, singola ripetizione del
  // motivo) che si interrompevano molto prima della melodia reale: il motivo viene
  // ripetuto qui per raggiungere una durata paragonabile (~18-19s), NON in loop a
  // runtime (nessuna ripetizione oltre il terminatore 0xFFFF, e' un unico array
  // piu' lungo scritto per intero, coerente con "un brano lungo suonato una volta").
  static const uint16_t TUNE_FUR_ELISE[] = {
    659, 622, 659, 622, 659, 494, 587, 523, 440,   // ripetuto 10x = 90 voci = 18.0s
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    0xFFFF
  };
  static const uint16_t TUNE_WARNING[] = {
    523, 784, 523, 784, 523, 784, 523, 784,        // ripetuto 12x = 96 voci = 19.2s
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    0xFFFF
  };
  static const uint16_t* TUNES[4] = {
    TUNE_ROMANCE, TUNE_WARNING, TUNE_FUR_ELISE, TUNE_ROMANCE
  };

  // Guadagno finale: catena analogica reale ~x40000 (gain del mixer MAME) * 0.6
  // (route gain del device "discrete" nel machine_config) rescalato /64 per il
  // contratto +/-512 di valueToBuffer(). Unico numero da ritarare se serve.
  const float PHOENIX_MASTER_SCALE = 40000.0f * 0.6f / 64.0f;

  uint8_t a = currentMachine->soundregs[0];   // sound A raw latch (0x6000)
  uint8_t b = currentMachine->soundregs[1];   // sound B raw latch (0x6800)

  // Melody trigger: MAME chiama mm6221aa_tune_w(data>>6) ad OGNI scrittura di
  // control_b_w, ma quello stesso latch porta anche Effect1 (bit0-5), riscritto
  // di continuo durante il gioco (es. esplosione navetta) — verificato che
  // questo produce letture transitorie con bit6-7=0 non legate alla melodia
  // (il gioco NON mantiene sempre una shadow-copy coerente). Il chip MM6221AA
  // reale suona il brano scelto UNA VOLA SOLA fino alla fine (confermato: non
  // va in loop, ~10s+, vedi TUNE_ROMANCE) e non si riavvia/interrompe per una
  // fluttuazione di un frame su un latch condiviso. Quindi:
  //  - (ri)parte da capo SOLO se il valore non-zero richiesto e' DIVERSO dalla
  //    tune ATTUALMENTE IN RIPRODUZIONE (non dall'ultimo valore letto: una
  //    rilettura dello stesso numero, anche dopo uno zero spurio, non tocca
  //    l'indice e lascia proseguire la riproduzione in corso);
  //  - un valore 0 non ferma mai nulla: la melodia si ferma SOLO da sola al
  //    proprio terminatore 0xFFFF (gestito piu' sotto).
  uint8_t new_tune = (b >> 6) & 0x03;
  if (new_tune != 0 && (!ph_mel_active || ph_mel_tune != new_tune)) {
    ph_mel_tune = new_tune; ph_mel_idx = 0; ph_mel_timer = 1; ph_mel_active = true;
  }

  int   e1_data = b & 0x0F;
  int   e1_freq = (b >> 4) & 0x01;
  int   e1_filt = (b >> 5) & 0x01;
  int   e2_data = a & 0x0F;
  int   e2_freq = (a >> 4) & 0x03;
  bool  noise_b40 = (a & 0x40) != 0;
  bool  noise_b80 = (a & 0x80) != 0;

  // NODE_30: capacita' selezionata per Effect2 (COMP_ADDER, cDefault=C18=0.01uF)
  float node30_c = 0.01e-6f;
  if (e2_freq & 1) node30_c += 0.47e-6f;   // C16
  if (e2_freq & 2) node30_c += 1.0e-6f;    // C17
  // NODE_31/32: bit alto di FREQ -> livello ampiezza Effect2 (SWITCH)
  float node32_level = (e2_freq & 2) ? (3.4f / 2.0f) : 3.4f;

  // Parametri 555 precalcolati. e1/e2b/e2cv hanno R/C fissi -> calcolati una volta
  // sola (static). e2a dipende da node30_c (cambia con e2_freq) -> ricalcolato ad
  // ogni chiamata (poche volte al secondo, non per-sample).
  static const ph555Params p_e1_555   = ph_make_555(47000.0f, 47000.0f, 1e-9f);
  static const ph555Params p_e2_555b  = ph_make_555(510000.0f, 510000.0f, 1e-6f);
  static const ph555Params p_e2_555cv = ph_make_555(20000.0f, 20000.0f, 1e-9f);
  const ph555Params p_e2_555a = ph_make_555(47000.0f, 100000.0f, node30_c);

  // Precalcolati (fissi): RCFILTER NODE_25 (Effect1 filtro), NODE_37 (Effect2 lento),
  // e i 3 canali del mixer finale (DC-block).
  static const float dt = 1.0f / 24000.0f;
  static const float exp_node25 = 1.0f - expf(-dt / ((1.0f/(1.0f/10000.0f+1.0f/100000.0f)) * 0.047e-6f));
  static const float exp_node37 = 1.0f - expf(-dt / (3051.98f * 100e-6f));
  static const float exp_mix1   = 1.0f - expf(-dt / (8507.46f * 10e-6f));   // canale Effect1
  static const float exp_mix2   = 1.0f - expf(-dt / (7500.0f  * 10e-6f));   // canale Effect2
  static const float exp_camp   = 1.0f - expf(-dt / (6628.0f  * 10e-6f));   // cAmp finale

  // Millman denominators as reciprocals: no hardware float division
  static const float MILLMAN35 = 1.0f / (1.0f/10000.0f + 1.0f/10200.0f + 1.0f/5000.0f);
  static const float MILLMAN38 = 1.0f / (1.0f/10000.0f + 1.0f/5100.0f + 1.0f/5000.0f);

  for (int i = 0; i < 64; i++) {
    // ── Rumore NE555 (bit 6-7 sound A) ──
    int32_t vc24 = ph_update_c24(ph_c24_level, ph_c24_counter, noise_b40);
    int32_t vc25 = ph_update_c25(ph_c25_level, ph_c25_counter, noise_b80);
    int32_t noise_level = (vc24 < vc25) ? (vc24 + (vc25 - vc24) / 2) : (vc25 + (vc24 - vc25) / 2);
    int32_t noise_freq  = 588 + 6325 * noise_level / 32768;
    ph_noise_counter -= noise_freq;
    if (ph_noise_counter <= 0) {
      long n = (-ph_noise_counter / 24000) + 1;
      ph_noise_counter += n * 24000;
      for (long k = 0; k < n; k++) {
        int fb = (((ph_noise_shiftreg >> 16) & 1) == ((ph_noise_shiftreg >> 17) & 1)) ? 1 : 0;
        ph_noise_shiftreg = (ph_noise_shiftreg << 1) | fb;
      }
      ph_noise_polybit = ph_noise_shiftreg & 1;
    }
    int32_t noise_sum = 0;
    if (!ph_noise_polybit) noise_sum += vc24;
    ph_noise_lp_counter -= 400;
    if (ph_noise_lp_counter <= 0) { ph_noise_lp_counter += 24000; ph_noise_lp_polybit = ph_noise_polybit; }
    if (!ph_noise_lp_polybit) noise_sum += vc25;
    float noise_out = ((float)(noise_sum / 2) - 16384.0f) / 64.0f;   // centrato + rescale

    // ── Effect 1 (sound B): RCDISC4 -> 555 CV -> NOTE -> switch/filtro ──
    ph_rcdisc4_step(ph_e1_vc1, e1_freq);
    float node21 = ph_555_step(ph_e1_555_cap, ph_e1_555_ff, p_e1_555,
                                5.0f, 5.0f, ph_e1_vc1, false, 0.0f);
    float node22 = ph_note_step(ph_e1_note_c1, ph_e1_note_c2, node21, e1_data);
    float node23 = e1_filt ? (3.4f * 100000.0f / 110000.0f) : 3.4f;
    float node24 = node22 * node23;
    float node25 = ph_rcfilter_step(ph_e1_rcfilt, node24, exp_node25);
    float effect1_snd = e1_filt ? node25 : node24;

    // ── Effect 2 (sound A): doppio 555 -> mixer -> filtro lento -> 555 CV -> NOTE ──
    float node33 = ph_555_step(ph_e2_555a_cap, ph_e2_555a_ff, p_e2_555a,
                                5.0f, 5.0f, -1.0f, true, 4.0f);
    float node34 = ph_555_step(ph_e2_555b_cap, ph_e2_555b_ff, p_e2_555b,
                                5.0f, 5.0f, -1.0f, true, 4.0f);
    float node35 = (node33 * (1.0f/10000.0f) + node34 * (1.0f/10200.0f) + 5.0f/5000.0f) * MILLMAN35;
    float node36 = (node34 + node35) * 0.5f;   // mixer2, R45=R46 -> media semplice
    float node37 = ph_rcfilter_step(ph_e2_rcfilt, node36, exp_node37);
    float node38 = (node33 * (1.0f/10000.0f) + node37 * (1.0f/5100.0f) + 5.0f/5000.0f) * MILLMAN38;
    float node39 = ph_555_step(ph_e2_555cv_cap, ph_e2_555cv_ff, p_e2_555cv,
                                5.0f, 5.0f, node38, false, 0.0f);
    float node40 = ph_note_step(ph_e2_note_c1, ph_e2_note_c2, node39, e2_data);
    float effect2_snd = node40 * node32_level;

    // ── Mixer finale (DISCRETE_MIXER4): DC-block per canale + Millman + cAmp + gain ──
    float vt1 = effect1_snd; ph_mix_vcap1 += (vt1 - ph_mix_vcap1) * exp_mix1; vt1 -= ph_mix_vcap1;
    float vt2 = effect2_snd; ph_mix_vcap2 += (vt2 - ph_mix_vcap2) * exp_mix2; vt2 -= ph_mix_vcap2;
    float mix_i = vt1 * (1.0f/57000.0f) + vt2 * (1.0f/30000.0f);
    float mix_v = mix_i * 6628.0f;
    ph_mix_vcamp += (mix_v - ph_mix_vcamp) * exp_camp;
    mix_v -= ph_mix_vcamp;

    float value = mix_v * PHOENIX_MASTER_SCALE + noise_out;

    // ── Melodia MM6221AA (approssimazione, invariata da SPINNERINO) ──
    // Il chip reale suona il brano in loop continuo finche' resta selezionato
    // (viene fermato solo dal trigger sopra, quando il gioco seleziona tune=0).
    // I nostri array sono approssimazioni brevi (~1.6-1.8s): al terminatore
    // 0xFFFF: NON e' un loop (confermato via confronto diretto con MAME dall'utente
    // — la melodia reale e' un brano lungo (~10s+) che suona una volta sola, non si
    // ripete). Il vero bug era che i nostri array TUNE_WARNING/TUNE_FUR_ELISE erano
    // placeholder troppo corti (~1.6-1.8s): allungati per coprire l'intera durata.
    if (ph_mel_active) {
      if (--ph_mel_timer == 0) {
        const uint16_t* t = TUNES[ph_mel_tune & 0x03];
        uint16_t f = t[ph_mel_idx];
        if (f == 0xFFFF) { ph_mel_active = false; ph_mel_freq = 0; }
        else {
          if (f != 0) ph_mel_freq = f;
          ph_mel_idx++;
          if (ph_mel_idx >= 100) ph_mel_idx = 99;
          ph_mel_timer = 24000 / 5;
        }
      }
      if (ph_mel_freq) {
        ph_mel_phase += (uint32_t)ph_mel_freq * 65536U / 24000U;
        int16_t m  = (ph_mel_phase & 0x8000) ? 28 : -28;
        int16_t m2 = ((ph_mel_phase << 1) & 0x8000) ? 14 : -14;
        value += m + m2;
      }
    }

    if (value > 512)  value = 512;
    if (value < -512) value = -512;
    valueToBuffer(i, (short)value);
  }
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

