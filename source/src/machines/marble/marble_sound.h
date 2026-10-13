#ifndef MARBLE_SOUND_H
#define MARBLE_SOUND_H

#include <stdint.h>
#include "marble_mem.h"

// Sound (MARBLE.md "Sound"): the arcade's 68010 sends one command byte per event to its
// sound CPU (0x158ac). The remake keeps the bytes; marble_rom.h maps them to songs of the
// C64 tune (race music, jingles, a silent song for stop) and effects (slot 1 stub).
//
//   logic, any module     send(m, cmd)  queue for this frame (file static, one machine)
//   marble::run_frame end step(s)       commands -> s.music / s.effect requests
//   marble::renderSid     s.music       SidPlayer::start(song)        (audio core)
//                         s.effect      SidPlayer::jsr(stub, effect)
namespace marble_sound {

static const uint32_t SKIP = 0x4003b8;  // word: 0x158ac sends nothing while set
static const uint8_t QUEUE = 8;

struct Sound {
  uint8_t song;              // emulation core: song playing, 0 none
  volatile uint16_t music;   // (requests << 8) | song
  volatile uint16_t effect;  // (requests << 8) | effect index
};

// no song, queue empty; the audio core sees a new music request (song 0: silence)
void reset(Sound &s);
// 0x158ac: queue cmd for the next step(); dropped while SKIP is set or the queue is full
void send(const marble_physics::Mem &m, uint8_t cmd);
void step(Sound &s);
uint8_t queued();

}  // namespace marble_sound

#endif
