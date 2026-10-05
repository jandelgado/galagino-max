#ifndef COMMANDO_MUSIC_H
#define COMMANDO_MUSIC_H

#include <stdint.h>

// Arcade sound latch ($C800) commands to C64 SID subtunes. From a MAME
// trace: 00-1b drive only the YM2203 SSG part (effects), 20-2c only FM
// (music), 2d stops the music, ff = idle. C64 subtunes: 1 theme, 2 high
// score, 3 short jingle.
enum MusicAction : uint8_t { MUSIC_NONE, MUSIC_PLAY, MUSIC_STOP };

struct MusicCue {
  uint8_t command;
  MusicAction action;
  uint8_t subtune;
};

static const MusicCue COMMANDO_CUES[] = {
  {0x20, MUSIC_PLAY, 1}, // game start jingle, then 21
  {0x21, MUSIC_PLAY, 1}, // in-game loop
  {0x22, MUSIC_PLAY, 1}, // respawn jingle, then 21
  {0x23, MUSIC_PLAY, 1}, // loop, not traced
  {0x24, MUSIC_PLAY, 1}, // loop, not traced
  {0x25, MUSIC_PLAY, 3}, // 8 s jingle, not traced
  {0x26, MUSIC_PLAY, 1}, // loop, not traced
  {0x27, MUSIC_PLAY, 3}, // game over
  {0x28, MUSIC_PLAY, 2}, // name entry loop
  {0x29, MUSIC_PLAY, 3}, // after name entry
  {0x2a, MUSIC_PLAY, 3}, // 6 s jingle, not traced
  {0x2c, MUSIC_PLAY, 1}, // loop, not traced
  {0x2d, MUSIC_STOP, 0},
  // 2b (death, 1 s) stays silent: 2d precedes it
};

// action for one latch write; MUSIC_NONE = effect or unknown, ignored
inline MusicCue commandoMusicCue(uint8_t command) {
  for (const MusicCue &cue : COMMANDO_CUES) {
    if (cue.command == command) {
      return cue;
    }
  }
  return MusicCue{command, MUSIC_NONE, 0};
}

#endif
