#ifndef SNK6502_SOUND_H
#define SNK6502_SOUND_H

#include <stdint.h>

// SNK 6502 boards' custom sound (Vanguard, Fantasy, Nibbler; MAME
// snk6502_a.cpp): two or three ROM-driven music voices with resistor-built
// waveforms, SN76477 noise, IMA-ADPCM effect and speech samples.
// Registers in soundregs, written by the game's sound port handler:
//   [0] [1] [2] [10] port latches, [3] speech phrase (0xff stop),
//   [4] speech strobe, [5] [6] port 0/1 write strobes,
//   [8] [9] [11] music 0/1/2 restart counters.
class Snk6502Sound {
public:
  // what the sound board reads from the rest of the game board
  class Host {
  public:
    virtual uint8_t soundRom(uint16_t addr) = 0;
    virtual bool musicMuted(int ch) = 0;
    virtual void musicEnded() {}  // Vanguard: music 0 ran out
    virtual const signed char *sample(uint8_t index) { return nullptr; }
    virtual uint32_t sampleLength(uint8_t index) { return 0; }
    virtual uint8_t sampleDivider(uint8_t index) { return 1; }

  protected:
    ~Host() {}
  };

  enum Board : uint8_t { VANGUARD, FANTASY };  // Fantasy/Nibbler: 3 voices, no effect samples

  Snk6502Sound(Host &host, Board board);

  // latch the strobe counters and mute flags; the machine's init() calls it
  // after machineBase::init() cleared the registers
  void reset(const uint8_t *regs);
  void render(const uint8_t *regs, int16_t *out, int n);

private:
  Host &host;
  Board board;
  uint32_t phase[3] = {0, 0, 0};
  uint16_t offset[3] = {0, 0, 0};
  uint32_t music_clock = 0;
  uint32_t noise_lfsr = 0x1ffff;  // SN76477 noise (shot B)
  uint8_t last_port0 = 0;
  uint8_t port_sequence[2] = {0, 0};
  uint8_t music0_restart = 0;
  uint8_t music1_restart = 0;
  uint8_t music2_restart = 0;
  bool music0_command_muted = true;
  bool music1_command_muted = true;
  bool music2_command_muted = true;
  bool muted[3] = {true, true, true};
  uint32_t sample_pos[3] = {0, 0, 0};  // fire, explosion, speech
  uint32_t sample_len[3] = {0, 0, 0};
  const signed char *sample_ptr[3] = {nullptr, nullptr, nullptr};
  int16_t adpcm_predictor[3] = {0, 0, 0};
  int8_t adpcm_step[3] = {0, 0, 0};
  uint8_t sample_repeat[3] = {0, 0, 0};
  uint8_t sample_divider[3] = {1, 1, 1};
  uint8_t speech_seen = 0;  // last speech strobe (regs[4]) handled
};

#endif
