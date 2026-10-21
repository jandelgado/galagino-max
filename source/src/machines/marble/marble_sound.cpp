#include "marble_sound.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

#include "marble_flow.h"
#include "marble_rom.h"

namespace marble_sound {

static uint8_t queue[QUEUE];
static uint8_t count = 0;

static uint16_t request(uint16_t r, uint8_t arg) { return (uint16_t)((((r >> 8) + 1) << 8) | arg); }

void reset(Sound &s) {
  s.song = 0;
  s.music = request(s.music, 0);
  count = 0;
}

void send(const marble_physics::Mem &m, uint8_t cmd) {
  if (m.rw(SKIP) || count == QUEUE) { return; }
  if (marble_flow::phase(m) == marble_flow::ATTRACT) { return; }  // MAME: attract is silent
  queue[count++] = cmd;
}

uint8_t queued() { return count; }

void step(Sound &s) {
  for (uint8_t i = 0; i < count; i++) {
    if (queue[i] & 0x80) { continue; }
    const MarbleSoundCmd &c = marble_sound_cmds[queue[i]];
    if (c.kind == MARBLE_SND_SONG && c.arg != s.song) {  // a running piece keeps playing
      s.song = c.arg;
      s.music = request(s.music, c.arg);
    }
    if (c.kind == MARBLE_SND_EFFECT) { s.effect = request(s.effect, c.arg); }
  }
  count = 0;
}

}  // namespace marble_sound

#endif  // GAME_MARBLE
