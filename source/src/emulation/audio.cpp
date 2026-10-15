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
    if(machineType == MCH_VANGUARD || machineType == MCH_FANTASY || machineType == MCH_NIBBLER)
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

