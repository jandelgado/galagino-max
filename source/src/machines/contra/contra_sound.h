#ifndef CONTRA_SOUND_H
#define CONTRA_SOUND_H

#include <stdint.h>

// Arcade sound commands (latch 001c) -> Gryzor.sid (C64, 11 songs).
// MAME sweep of all 256 commands: 00 stop, 02-3b effects (< 2.5 s),
// 40-4d music (4d title). The C64 tune has 2 songs (1 title, 2 in game)
// and 9 effects (songs 3-11, driver entry $04f1 with A = 0-8); effects are
// matched to the frequent arcade ones by length and noise (13 player shot,
// 27 / 29 explosions), the rest stays silent.
enum ContraCueKind : uint8_t { CUE_NONE, CUE_STOP, CUE_MUSIC, CUE_FX };

struct ContraCue {
  ContraCueKind kind;
  uint8_t arg;   // song 1-2 or effect 0-8
};

static const uint8_t CONTRA_SND_STOP = 0x00;
static const uint8_t CONTRA_SND_TITLE = 0x4d;
static const uint8_t CONTRA_SND_MUSIC_FIRST = 0x40;
static const uint8_t SID_SONG_TITLE = 1, SID_SONG_GAME = 2;

// arcade effect command -> C64 effect
static const struct {
  uint8_t cmd, fx;
} CONTRA_FX[] = {
    {0x13, 3}, {0x15, 4}, {0x12, 0}, {0x27, 5}, {0x29, 7},
    {0x0f, 6}, {0x10, 8}, {0x35, 1}, {0x39, 7}, {0x37, 2},
};

static inline ContraCue contraCue(uint8_t cmd) {
  if (cmd == CONTRA_SND_STOP) {
    return {CUE_STOP, 0};
  }
  if (cmd == CONTRA_SND_TITLE) {
    return {CUE_MUSIC, SID_SONG_TITLE};
  }
  if (cmd >= CONTRA_SND_MUSIC_FIRST && cmd < CONTRA_SND_TITLE) {
    return {CUE_MUSIC, SID_SONG_GAME};
  }
  for (const auto &e : CONTRA_FX) {
    if (e.cmd == cmd) {
      return {CUE_FX, e.fx};
    }
  }
  return {CUE_NONE, 0};
}

#endif
