#ifndef MARBLE_SOUND_H
#define MARBLE_SOUND_H

#include <stdint.h>
#include "marble_mem.h"

// Sound (MARBLE.md "Sound"): the arcade's 68010 sends one command byte per event to its
// sound CPU (0x158ac). The remake keeps the bytes; marble_rom.h maps the music and the
// jingle to songs of the C64 tune, everything else goes to the arcade's sound program
// (effects, stops); stop also starts the C64 tune's silent song.
//
//   logic, any module     send(m, cmd)  queue for this frame (file static, one machine)
//   marble::run_frame end step(s, cmds) commands -> s.music requests, cmds for the board
//   marble::renderAudio   s.music       SidPlayer::start(song)        (audio core)
namespace marble_sound {

static const uint32_t SKIP = 0x4003b8;  // word: 0x158ac sends nothing while set
static const uint32_t GAME_MODE = 0x400394;  // word: MODE_SINGLE keeps pair() to 0x3a
static const uint16_t MODE_SINGLE = 2;

// arcade command bytes the remake sends (MARBLE.md "Sound")
enum Cmd : uint8_t {
  SND_STOP = 0x00, SND_TIME_OUT = 0x09, SND_JUMP = 0x31, SND_STEELIE_HIT = 0x32,
  SND_CAUGHT = 0x33, SND_PLAYERS_HIT = 0x37, SND_BREAK = 0x38, SND_DIZZY = 0x39,
  SND_TUBE = 0x35, SND_FUNNEL = 0x36, SND_PAIR_A = 0x3a, SND_PAIR_B = 0x3b,
  SND_LOST_EMBEDDED = 0x3c, SND_LOST = 0x3d, SND_CARRIED = 0x3e, SND_TIME_OUT_JINGLE = 0x3f,
  SND_BOUNCE = 0x42, SND_TIME_OUT_10 = 0x44, SND_FALL = 0x45, SND_LAUNCH = 0x45, SND_DROP = 0x46,
  SND_TIME_BONUS = 0x53, SND_LANDING = 0x5d,
  SND_DISSOLVE = 0x5e, SND_SHARDS = 0x5f, SND_GAME_OVER = 0x61
};
static const uint8_t QUEUE = 8;

struct Sound {
  uint8_t song;              // emulation core: song playing, 0 none
  volatile uint16_t music;   // (requests << 8) | song
};

// no song, queue empty; the audio core sees a new music request (song 0: silence)
void reset(Sound &s);
// 0x158ac: queue cmd for the next step(); dropped while SKIP is set, in attract mode or when
// the queue is full
void send(const marble_physics::Mem &m, uint8_t cmd);
// 0x15884: 0x3a, then 0x3b unless GAME_MODE is MODE_SINGLE (0x3a stops the effect channels:
// dizzy loops until then)
void pair(const marble_physics::Mem &m);
// the queued commands: songs to s.music, the others into board (QUEUE bytes, nullptr: dropped);
// -> the number for the board
uint8_t step(Sound &s, uint8_t *board = nullptr);
uint8_t queued();

}  // namespace marble_sound

#endif
