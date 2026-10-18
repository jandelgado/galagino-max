#ifndef GNG_MUSIC_H
#define GNG_MUSIC_H

#include <stdint.h>

// Arcade sound latch ($3A00) commands to C64 SID songs. The sound driver
// decodes bits 0-5; ff is the idle write after every command. From MAME
// (2026-10-10): 01-24 drive only the SSG part (effects, left to the sound
// CPU), 25-3e only FM (music and jingles, dropped). Game flow: 35 credit,
// 36 game start, 3c 3d 3a intro, 30 player ready, 2b stage 1, 3f 00 31
// death, 3f 00 2f game over. Other long FM tunes (later stages, untraced)
// play the main theme. C64 song 2 (PSID default, init $3B14) is the main
// theme (checked on the device), song 1 the other tune.
enum GngMusic : uint8_t { GNG_NONE, GNG_PLAY, GNG_STOP };

struct GngCue {
  GngMusic action;
  uint8_t song; // GNG_PLAY only
};

static const uint8_t GNG_CMD_MASK = 0x3f;
static const uint8_t GNG_CMD_IDLE = 0xff;
static const uint8_t GNG_FIRST_FM = 0x25;
static const uint8_t GNG_SONG_THEME = 2;
static const uint8_t GNG_SONG_START = 1;

// FM tunes that loop (MAME: still keyed on after 10 s)
static const uint8_t GNG_LOOPS[] = {
  0x26, 0x28, 0x29, 0x2a, 0x2b, 0x2d, 0x2e, 0x32, 0x33, 0x34, 0x37,
};

// action for one latch write
inline GngCue gngMusicCue(uint8_t command) {
  if (command == GNG_CMD_IDLE) {
    return GngCue{GNG_NONE, 0};
  }

  const uint8_t cmd = command & GNG_CMD_MASK;
  if (cmd == 0x00) {
    return GngCue{GNG_STOP, 0};
  }
  if (cmd < GNG_FIRST_FM) {
    return GngCue{GNG_NONE, 0};
  }
  if (cmd == 0x35 || cmd == 0x36) {
    return GngCue{GNG_PLAY, GNG_SONG_START};
  }
  for (uint8_t loop : GNG_LOOPS) {
    if (loop == cmd) {
      return GngCue{GNG_PLAY, GNG_SONG_THEME};
    }
  }
  return GngCue{GNG_STOP, 0}; // jingles: silence while the arcade plays FM
}

#endif
