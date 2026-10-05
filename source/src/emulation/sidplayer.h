#ifndef SIDPLAYER_H
#define SIDPLAYER_H

#include <stdint.h>
#include "sid.h"
#include "../cpus/m6502/m6502.h"

// PSID fields the player needs (HVSC SID_file_format.txt)
struct PsidInfo {
  uint16_t load, init, play;
  uint8_t songs, start_song;
  const uint8_t *payload; // C64 bytes from `load` on, points into the file
  uint16_t payload_len;
};

// false for RSID, IRQ-installed players (play = 0), CIA-timed songs and
// truncated files: the player only emulates JSR init / JSR play at 50 Hz
bool psidParse(const uint8_t *file, uint32_t len, PsidInfo *info);

// Plays a PSID tune on Sid: the tune's own 6502 code runs on m6502, play
// is called every 480 samples (50 Hz PAL VBI), independent of video.
// C64 memory is only $0000-$01FF (internal) and [load, load + ram_size)
// (caller owned, e.g. Arena). Other accesses count as strays().
//
//   m6502 JSR init/play --> $0000-$01FF  low[]
//                       --> load..       ram (tune code + data)
//                       --> $D400-$D41F  Sid registers
//                       --> elsewhere    strays()
class SidPlayer {
public:
  static const uint32_t PLAY_HZ = 50;

  // false if the payload does not fit ram
  bool load(const PsidInfo &info, uint8_t *ram, uint16_t ram_size);
  // song 1..songs: fresh payload copy, SID reset, JSR init with A = song - 1
  void start(uint8_t song);
  void render(int16_t *out, int n);
  uint32_t strays() const { return stray_count; }

private:
  static const uint32_t SAMPLES_PER_PLAY = Sid::SAMPLE_RATE / PLAY_HZ;
  static const uint32_t INIT_MAX_CYCLES = 1000000;
  static const uint32_t PLAY_MAX_CYCLES = 20000; // PAL frame: 19656
  static const uint16_t LOW_SIZE = 0x200;        // zero page + stack
  static const uint16_t STACK = 0x100;
  static const uint16_t RETURN_PC = 0xffff;      // fake JSR return address
  static const uint16_t SID_BASE = 0xd400;
  static const uint16_t SID_END = 0xd420;        // $D400-$D41F incl. read-only

  static uint8_t read(m6502_t *cpu, uint16_t addr);
  static void write(m6502_t *cpu, uint16_t addr, uint8_t val);
  bool call(uint16_t addr, uint8_t a, uint32_t max_cycles);

  m6502_t cpu;
  Sid sid;
  PsidInfo info = {};
  uint8_t *ram = nullptr;
  uint16_t ram_size = 0;
  uint8_t low[LOW_SIZE];
  uint32_t samples_to_play = 0;
  uint32_t stray_count = 0;
  bool playing = false;
};

#endif
