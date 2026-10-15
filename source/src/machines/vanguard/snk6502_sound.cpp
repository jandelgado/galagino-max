#include "snk6502_sound.h"
#include "../../emulation/ima_adpcm.h"

using ima_adpcm::ima_adpcm_decode;

Snk6502Sound::Snk6502Sound(Host &host, Board board) : host(host), board(board) {}

void Snk6502Sound::reset(const uint8_t *regs) {
  phase[0] = phase[1] = phase[2] = 0;
  offset[0] = offset[1] = offset[2] = 0;
  music_clock = 0;
  noise_lfsr = 0x1ffff;
  last_port0 = 0;
  port_sequence[0] = regs[5];
  port_sequence[1] = regs[6];
  music0_restart = regs[8];
  music1_restart = regs[9];
  music2_restart = regs[11];
  music0_command_muted = host.musicMuted(0);
  music1_command_muted = host.musicMuted(1);
  music2_command_muted = host.musicMuted(2);
  muted[0] = music0_command_muted;
  muted[1] = music1_command_muted;
  muted[2] = music2_command_muted;
  for (int i=0;i<3;i++) {
    sample_pos[i]=sample_len[i]=0;sample_ptr[i]=nullptr;
    adpcm_predictor[i]=adpcm_step[i]=sample_repeat[i]=0;sample_divider[i]=1;
  }
  speech_seen = regs[4];
}

void Snk6502Sound::render(const uint8_t *regs, int16_t *out, int n) {
  const bool is_fantasy = board == FANTASY;
  const uint8_t p0 = regs[0];
  const uint8_t p1 = regs[1];
  const uint8_t shapes = regs[2];
  const uint8_t p3 = regs[10];

  // These controls are evaluated on a CPU write, not continuously from the
  // latched level. Continuously resetting offset 0 made the opening tune stick.
  if(regs[5]!=port_sequence[0]){
    port_sequence[0]=regs[5];
    if (!is_fantasy && (p0 & 0x20) && !(last_port0 & 0x20)) {
      sample_ptr[0]=host.sample(0);
      sample_len[0]=host.sampleLength(0);sample_pos[0]=0;
      sample_divider[0]=host.sampleDivider(0);sample_repeat[0]=0;
    } else if (!(p0&0x20) && (last_port0&0x20)) sample_len[0]=0;
    if (!is_fantasy && (p0 & 0x80) && !(last_port0 & 0x80)) {
      sample_ptr[1]=host.sample(1);
      sample_len[1]=host.sampleLength(1);sample_pos[1]=0;
      sample_divider[1]=host.sampleDivider(1);sample_repeat[1]=0;
    }
    last_port0=p0;
  }
  if(regs[8]!=music0_restart){
    music0_restart=regs[8];offset[0]=0;muted[0]=false;
  }
  bool command_muted=host.musicMuted(0);
  if(command_muted!=music0_command_muted){
    music0_command_muted=command_muted;
    if(command_muted){muted[0]=true;offset[0]=0;}
    else {muted[0]=false;offset[0]=0;}
  }
  port_sequence[1]=regs[6];
  if(regs[9]!=music1_restart){
    music1_restart=regs[9];offset[1]=0;muted[1]=false;
  }
  bool command_muted1=host.musicMuted(1);
  if(command_muted1!=music1_command_muted){
    music1_command_muted=command_muted1;
    if(command_muted1){muted[1]=true;offset[1]=0;}
    else {muted[1]=false;offset[1]=0;}
  }
  if(regs[11]!=music2_restart){
    music2_restart=regs[11];offset[2]=0;muted[2]=false;
  }
  bool command_muted2=host.musicMuted(2);
  if(command_muted2!=music2_command_muted){
    music2_command_muted=command_muted2;
    if(command_muted2){muted[2]=true;offset[2]=0;}
    else {muted[2]=false;offset[2]=0;}
  }
  uint8_t speech_sequence=regs[4];
  if(speech_sequence!=speech_seen){
    speech_seen=speech_sequence;
    uint8_t phrase=regs[3];
    if(phrase==0xff)sample_len[2]=0;
    else if(sample_pos[2]>=sample_len[2]){
      sample_ptr[2]=host.sample(2+phrase);
      sample_len[2]=host.sampleLength(2+phrase);sample_pos[2]=0;
      sample_divider[2]=host.sampleDivider(2+phrase);sample_repeat[2]=0;
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

  for (int i = 0; i < n; i++) {
    int sample = 0;
    for (int ch = 0; ch < (is_fantasy ? 3 : 2); ch++) {
      if (muted[ch]) continue;
      unsigned short base;
      if(ch==2)base=0x1000|((p3&0x70)<<4);
      else base=(ch ? 0x0800 : 0)|((ch ? p1 : p0)&7)*0x100;
      uint8_t note = host.soundRom(base + offset[ch]);
      if (note == 0xff) continue;
      uint32_t step = 939349UL / (256 - note); // 43000*8/24000 in Q16
      uint32_t next_phase = phase[ch] + step;
      int previous = form[ch][(phase[ch] >> 16) & 15];
      int current = form[ch][(next_phase >> 16) & 15];
      uint32_t fraction = next_phase & 0xffff;
      sample += ((previous * (int)(0x10000 - fraction) +
                  current * (int)fraction) >> 16) * 16;
      phase[ch] = next_phase;
    }

    // IMA-ADPCM samples: effects at 8 kHz, speech at 6 kHz, held to 24 kHz.
    for(int voice=0;voice<3;voice++)if(sample_ptr[voice] && sample_pos[voice]<sample_len[voice]){
      if(!sample_repeat[voice]){
        if(!sample_pos[voice]){
          const uint8_t *encoded=(const uint8_t *)sample_ptr[voice];
          adpcm_predictor[voice]=(int16_t)(encoded[0]|(encoded[1]<<8));adpcm_step[voice]=0;
        }else{
          uint32_t nibble=sample_pos[voice]-1;
          uint8_t packed=(uint8_t)sample_ptr[voice][2+(nibble>>1)];
          uint8_t code=(nibble&1)?(packed>>4):(packed&15);
          ima_adpcm_decode(adpcm_predictor[voice], adpcm_step[voice], code);
        }
        sample_pos[voice]++;sample_repeat[voice]=sample_divider[voice];
      }
      sample+=(adpcm_predictor[voice]>>8);sample_repeat[voice]--;
    }

    // Shot B is the cabinet's second SN76477 and remains synthesized noise.
    uint32_t feedback = (noise_lfsr ^ (noise_lfsr >> 3)) & 1;
    noise_lfsr = (noise_lfsr >> 1) | (feedback << 16);
    int noise = (noise_lfsr & 1) ? 1 : -1;
    if (!is_fantasy && (p0 & 0x40)) sample += noise * 70;
    if (is_fantasy && (p0 & 0x80)) sample += noise * 55;

    music_clock += is_fantasy ? 416 : 3899;
    uint32_t music_threshold=is_fantasy ? 240000 : 2400000;
    if (music_clock >= music_threshold) {
      music_clock -= music_threshold;
      for (int ch=0;ch<(is_fantasy?3:2);ch++) offset[ch]=(offset[ch]+1)&0xff;
      if (!is_fantasy && offset[0] == 0) {
        muted[0]=true;
        music0_command_muted=true;
        host.musicEnded();
      }
    }
    if (sample > 500) sample = 500; else if (sample < -500) sample = -500;
    out[i] = sample;
  }
}
