#ifndef COMMANDO_MUSIC_H
#define COMMANDO_MUSIC_H

#include <stdint.h>

// Arcade sound latch ($C800) commands to C64 SID subtunes. From a MAME
// trace: 00-1b drive only the YM2203 SSG part (effects), 20-2c only FM
// (music), 2d stops the music, ff = idle. Untraced roles from the main
// ROM ($86BB stubs). C64 subtunes: 1 theme, 2 high score, 3 area cleared,
// 4-19 sound effects (voices 0 + 1, end silent): 4 grenade pickup, 8
// death and game over, 16 bonus life. Effects play over the music, which
// loses their voices meanwhile.
enum MusicAction : uint8_t { MUSIC_NONE, MUSIC_PLAY, MUSIC_STOP, MUSIC_EFFECT };

struct MusicCue {
  uint8_t command;
  MusicAction action;
  uint8_t subtune;
};

static const MusicCue COMMANDO_CUES[] = {
  {0x18, MUSIC_EFFECT, 4}, // grenade pickup
  {0x1b, MUSIC_EFFECT, 16}, // bonus life
  {0x20, MUSIC_PLAY, 1}, // game start jingle, then 21
  {0x21, MUSIC_PLAY, 1}, // in-game loop
  {0x22, MUSIC_PLAY, 1}, // respawn jingle, then 21
  {0x23, MUSIC_PLAY, 1}, // area end reached, before 25
  {0x24, MUSIC_PLAY, 1}, // 21 variant, ($EDA9 & 3) == 3
  {0x25, MUSIC_PLAY, 3}, // area cleared
  {0x26, MUSIC_PLAY, 1}, // loop, ($EDA9 & 3) == 3
  {0x27, MUSIC_EFFECT, 8}, // game over
  {0x28, MUSIC_PLAY, 2}, // name entry loop
  {0x29, MUSIC_PLAY, 3}, // after name entry, rank 1
  {0x2a, MUSIC_PLAY, 3}, // after name entry, other ranks
  {0x2b, MUSIC_EFFECT, 8}, // player dies
  {0x2c, MUSIC_PLAY, 1}, // 22 variant, ($EDA9 & 3) == 3
  {0x2d, MUSIC_STOP, 0},
};

// action for one latch write; MUSIC_NONE = SSG effect or unknown, ignored
inline MusicCue commandoMusicCue(uint8_t command) {
  for (const MusicCue &cue : COMMANDO_CUES) {
    if (cue.command == command) {
      return cue;
    }
  }
  return MusicCue{command, MUSIC_NONE, 0};
}

#endif
