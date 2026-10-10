#ifndef SIDPLAYER_H
#define SIDPLAYER_H

#include <stdint.h>
#include "sid.h"
#include "../cpus/m6502/m6502.h"

// PSID fields the player needs (HVSC SID_file_format.txt)
struct PsidInfo {
  uint16_t load, init, play;
  uint8_t songs, start_song;
  SidClock clock; // PSID v2+ flags, NTSC only if marked NTSC-only
  const uint8_t *payload; // C64 bytes from `load` on, points into the file
  uint16_t payload_len;
};

// false for RSID, IRQ-installed players (play = 0), CIA-timed songs and
// truncated files: the player only emulates JSR init / JSR play at 50 Hz
bool psidParse(const uint8_t *file, uint32_t len, PsidInfo *info);

// Plays a PSID tune on Sid: the tune's own 6502 code runs on m6502, play
// is called every 480 samples (50 Hz PAL VBI) or 400 (60 Hz NTSC),
// independent of video.
// C64 memory is only $0000-$01FF (internal) and [load, load + ram_size)
// (caller owned, e.g. Arena). Other accesses count as strays().
//
//   m6502 JSR init/play --> $0000-$01FF  low[]
//                       --> load..       ram (tune code + data)
//                       --> $D400-$D41F  Sid registers
//                       --> elsewhere    strays()
class SidPlayer {
public:
  static const uint32_t PAL_PLAY_HZ = 50;
  static const uint32_t NTSC_PLAY_HZ = 60;

  // false if the payload does not fit ram
  bool load(const PsidInfo &info, uint8_t *ram, uint16_t ram_size);
  // song 1..songs: fresh payload copy, SID reset, JSR init with A = song - 1
  void start(uint8_t song);
  // JSR addr with A on the running tune, no reload and no SID reset (e.g.
  // a driver's sound effect entry); ignored before start()
  void jsr(uint16_t addr, uint8_t a);
  // C64 [from, from + size) mirrors [to, to + size) in the RAM window, for
  // tunes that copy their player elsewhere (GnG: $4000-$4CFF to $C000).
  // Call after load(), which clears it.
  void alias(uint16_t from, uint16_t to, uint16_t size);
  void render(int16_t *out, int n);
  uint32_t strays() const { return stray_count; }

private:
  static const uint32_t INIT_MAX_CYCLES = 1000000;
  static const uint32_t PLAY_MAX_CYCLES = 20000; // PAL frame: 19656, NTSC 17045
  static const uint16_t LOW_SIZE = 0x200;        // zero page + stack
  static const uint16_t STACK = 0x100;
  static const uint16_t RETURN_PC = 0xffff;      // fake JSR return address
  static const uint16_t SID_BASE = 0xd400;
  static const uint16_t SID_END = 0xd420;        // $D400-$D41F incl. read-only

  static uint8_t read(m6502_t *cpu, uint16_t addr);
  static void write(m6502_t *cpu, uint16_t addr, uint8_t val);
  bool call(uint16_t addr, uint8_t a, uint32_t max_cycles);
  uint8_t *aliased(uint16_t addr);

  m6502_t cpu;
  Sid sid;
  PsidInfo info = {};
  uint8_t *ram = nullptr;
  uint16_t ram_size = 0;
  uint16_t alias_from = 0, alias_to = 0, alias_size = 0;
  uint8_t low[LOW_SIZE];
  uint32_t samples_per_play = 0;
  uint32_t samples_to_play = 0;
  uint32_t stray_count = 0;
  bool playing = false;
};

#endif
