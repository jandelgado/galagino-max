#include "marble_flow.h"
#include "marble_physics.h"

#ifdef ENABLE_MARBLE

// Port of tools/marble_flow.py (Marble Love TS: game-tick-timers.ts, timer-cascade.ts,
// main-loop-init-1101e.ts, player-slot-iter-118d2.ts, state-sub-18a88.ts). Frame counts of
// the blocking main loop parts come from MAME frame traces (+-1 frame).

namespace marble_flow {

namespace {

using marble_physics::Mem;
using marble_physics::PLAYER;

// player fields
enum : uint8_t {
  SLOT = 0x18, PLAYER_NO = 0x19, STATE = 0x1a,
  TIMER = 0x6a, TENTHS = 0x6c, TICKS = 0x6e, FLASH = 0x71,  // race timer 0x6a-0x6e
  FADE = 0x70, SCORE = 0xbc, DEATHS = 0xd2, SKIP = 0xd8,
};
static const uint8_t TIMER_BYTES = 6, STOPPED = 0xff, TICKS_START = 5, TENTHS_START = 9;
static const uint8_t OUT_OF_LIST = 2;           // +0x18 after the time out
static const uint8_t TIME_BONUS = 8;            // +0x1a: 0x28a96 skips the timer

static const uint32_t RNG = 0x4003a6, SCORED = 0x40039c;

// race intro: races 0/1 set their time, later races add it (+5 s steps)
static const uint8_t RACE_TIME[] = {60, 60, 35, 30, 20, 20};
static const uint8_t LAST_RACE = 5;
static const uint8_t ADD_STEP = 10, ADD_SECONDS = 5;
static const uint8_t INTRO_END = 61;            // last arm to race start: 61 + race time
static const uint8_t START_FIRST_ADD = 131;     // new game: init to the first +5 s (scroll in)
static const uint8_t FRAMES_TO_START = 6;       // 0x400010 vblank acks during the race end

// race end (offsets from the 0x400390 = 3 frame), per race just finished
static const uint8_t NEXT_INDEX = 2;            // race index + 1 written
static const uint8_t BONUS_WAIT = 40;           // 0x118d2 wait before the level load
static const uint8_t LOAD[] = {81, 80, 60, 116, 73};   // 0x16ec6 level load of the next race
static const uint8_t INIT_AFTER[] = {8, 8, 8, 9, 9};   // bonus to the next race init
static const uint8_t FIRST_ADD[] = {64, 63, 63, 64, 64};  // next race init to its first +5 s
static const uint8_t SECONDS_MAX = 99;          // bonus and final bonus clamp
static const uint8_t BONUS_POINTS = 100;        // per second left

// final bonus (0x18a88)
static const int32_t FINAL_BASE = 20000, FINAL_SECOND = 1000, FINAL_DEATH = 1000;
static const uint8_t DEATHS_MAX = 20;
static const uint8_t FINAL_AFTER = 6;           // bonus to state 6
static const uint8_t FINAL_FIRST = 10, FINAL_STEP_FRAMES = 2;
static const int32_t FINAL_STEP = 250;
static const uint16_t FINAL_END = 272;          // last step to game over (0x5a + 0xb4 waits)

static const uint16_t TIME_OUT_FRAMES = 182;    // 0x16a20 summary

// palette pulses (0x26bee score fade, 0x26c78 timer flash): counters wrap above these
static const int8_t FADE_WRAP = 0x3f, FLASH_WRAP = 0x1f;
static const uint8_t OFF = 0xff;
static const int16_t FLASH_ON = 4, FLASH_OFF = 5;  // 0x28a96: seconds when a second passes

int16_t seconds(const Mem &m) { return m.sw(PLAYER + TIMER); }

// 0x28608: points to the player score, its score changed flag
void addPoints(Mem &m, int32_t points) {
  m.wl(PLAYER + SCORE, m.rl(PLAYER + SCORE) + (uint32_t)points);
  const uint8_t bit = m.rb(PLAYER + PLAYER_NO) & 0x3f;
  if (bit < 8) { m.wb(SCORED, m.rb(SCORED) | (uint8_t)(1u << bit)); }
}

int16_t clamp(int16_t v, int16_t hi) { return v > hi ? hi : v; }

// 0x28c38 on the player timer: bit 0 seconds wrapped below 0, bit 1 a tenth passed
uint8_t cascade(Mem &m) {
  const uint32_t t = PLAYER + TIMER;
  if (m.rb(t + 4) == STOPPED) { return 0; }

  m.wb(t + 4, m.rb(t + 4) - 1);
  if (m.sb(t + 4) >= 0) { return 0; }
  m.wb(t + 4, TICKS_START);

  m.wb(t + 2, m.rb(t + 2) - 1);
  if (m.sb(t + 2) >= 0) { return 0; }
  m.wb(t + 2, TENTHS_START);

  m.ww(t, m.rw(t) - 1);
  return m.rw(t) == 0xffff ? 3 : 2;
}

// vblank anim ticks 0x26bee / 0x26c78 (before the race timer): step the pulse counters; the
// HUD picks the colours from them (marble_hud)
void pulses(Mem &m) {
  uint8_t c = m.rb(PLAYER + FADE);
  if (c != OFF && !m.rb(PLAYER + SKIP)) {
    c++;
    m.wb(PLAYER + FADE, (int8_t)c > FADE_WRAP ? 0 : c);
  }
  c = m.rb(PLAYER + FLASH);
  if (c != OFF) {
    c++;
    m.wb(PLAYER + FLASH, (int8_t)c > FLASH_WRAP ? 0 : c);
  }
}

// race intro of the loaded race: +5 s steps, then the timer starts and the race is on
void introStep(Flow &f, Mem &m) {
  const uint16_t first = firstAdd(f);
  const uint8_t time = RACE_TIME[f.race];
  const uint16_t i = ++f.intro;  // frames since the race init
  if (i >= first && (i - first) % ADD_STEP == 0 && (i - first) / ADD_STEP < time / ADD_SECONDS) {
    m.ww(PLAYER + TIMER, m.rw(PLAYER + TIMER) + ADD_SECONDS);
  }
  if (i != first - 1 + INTRO_END + time) { return; }

  m.wb(PLAYER + TICKS, TICKS_START);
  m.ww(GAME_STATE, RACE);
  f.intro = NO_INTRO;
  f.fresh = false;
}

// RACE_END: bonus, then the next race load or the final bonus (frames since 0x400390 = 3)
Step raceEnd(Flow &f, Mem &m) {
  const uint8_t race = f.race;
  const uint16_t bonus = NEXT_INDEX + BONUS_WAIT + (race < LAST_RACE ? LOAD[race] : 0);
  if (f.frame == NEXT_INDEX) {
    m.ww(marble_physics::RACE, race + 1);
    m.wb(PLAYER + SKIP, 0);  // 0x118d2: score fade on, timer flash off
    m.wb(PLAYER + FLASH, OFF);
    m.wb(PLAYER + FADE, 0);
  }
  if (f.frame == bonus) {
    m.wb(PLAYER + FADE, OFF);  // 0x118d2: score fade off with the points
    addPoints(m, clamp(seconds(m), SECONDS_MAX) * BONUS_POINTS);
  }

  if (race == LAST_RACE) {
    if (f.frame == bonus + FINAL_AFTER) {
      f.final = FINAL_BASE + clamp(seconds(m), SECONDS_MAX) * FINAL_SECOND -
                clamp((int16_t)m.rw(PLAYER + DEATHS), DEATHS_MAX) * FINAL_DEATH;
      m.ww(GAME_STATE, FINAL);
    }
    return WAIT;
  }
  if (f.frame != bonus + INIT_AFTER[race]) { return WAIT; }

  f.race = race + 1;
  return START_RACE;
}

// FINAL: the final bonus in 250 point steps, then game over
void finalStep(Flow &f, Mem &m) {
  if (f.final > 0) {
    if (f.frame < FINAL_FIRST || (f.frame - FINAL_FIRST) % FINAL_STEP_FRAMES) { return; }
    f.final -= FINAL_STEP;
    addPoints(m, FINAL_STEP);
    if (f.final <= 0) { f.frame = 0; }  // count FINAL_END from the last step
    return;
  }
  if (f.frame == FINAL_END) { m.ww(GAME_STATE, GAME_OVER); }
}

}  // namespace

void newGame(Flow &f) {
  f.phase = GAME_START;
  f.frame = 0;
  f.intro = NO_INTRO;
  f.race = 0;
  f.fresh = true;
  f.final = 0;
}

uint8_t raceTime(uint8_t race) { return RACE_TIME[race <= LAST_RACE ? race : LAST_RACE]; }

uint16_t firstAdd(const Flow &f) { return f.fresh ? START_FIRST_ADD : FIRST_ADD[f.race - 1]; }

bool timer(Mem &m) {
  if (m.rb(PLAYER + STATE) == TIME_BONUS) { return false; }
  const uint8_t flags = cascade(m);
  if (flags & 2) {  // a second passed: timer flash from 4 s on, off above 5 s
    if (seconds(m) == FLASH_ON) { m.wb(PLAYER + FLASH, 0); }
    if (seconds(m) > FLASH_OFF) { m.wb(PLAYER + FLASH, OFF); }
  }
  if (!(flags & 1)) { return false; }

  // 0x28a96: out of play, timer cleared and stopped
  m.wb(PLAYER + SLOT, OUT_OF_LIST);
  m.ww(PLAYER + TIMER, 0);
  m.wb(PLAYER + TENTHS, 0);
  m.wb(PLAYER + TICKS, STOPPED);
  m.wb(PLAYER + FLASH, 0xff);
  m.ww(GAME_STATE, TIME_OUT);
  return true;
}

Step frame(Flow &f, Mem &m, bool start) {
  if (f.fresh && f.intro == NO_INTRO) { return START_RACE; }  // new game: load practice

  // a phase set last frame (logic tick, timer, sequencer) counts from 1 here
  if (phase(m) != f.phase) {
    f.phase = phase(m);
    f.frame = 0;
  }
  f.frame++;
  pulses(m);
  timer(m);
  if (f.intro != NO_INTRO) { introStep(f, m); }
  if (phase(m) != f.phase) { return WAIT; }  // time out or race start: from next frame on

  switch (f.phase) {
    case RACE: return f.intro == NO_INTRO ? LOGIC : WAIT;
    case RACE_END: return f.intro == NO_INTRO ? raceEnd(f, m) : WAIT;
    case FINAL: finalStep(f, m); return WAIT;
    case TIME_OUT:
      if (f.frame == TIME_OUT_FRAMES) { m.ww(GAME_STATE, GAME_OVER); }
      return WAIT;
    case GAME_OVER:
      if (start) { newGame(f); }
      return WAIT;
    default: return WAIT;
  }
}

Carry carry(const Flow &f, const Mem &m) {
  Carry c = {};
  c.rng = m.rw(RNG);
  c.frames = m.rl(marble_physics::FRAMES);
  if (f.fresh) { return c; }

  for (uint8_t k = 0; k < TIMER_BYTES; k++) { c.timer[k] = m.rb(PLAYER + TIMER + k); }
  c.score = m.rl(PLAYER + SCORE);
  c.deaths = m.rw(PLAYER + DEATHS);
  return c;
}

void restore(Mem &m, const Carry &c) {
  m.ww(RNG, c.rng);
  m.wl(marble_physics::FRAMES, c.frames + FRAMES_TO_START);
  for (uint8_t k = 0; k < TIMER_BYTES; k++) { m.wb(PLAYER + TIMER + k, c.timer[k]); }
  m.wl(PLAYER + SCORE, c.score);
  m.ww(PLAYER + DEATHS, c.deaths);
}

// race init (0x10504 player part): the timer waits for the intro; races 0/1 start from 0 s
void raceLoaded(Flow &f, Mem &m) {
  if (f.race < 2) { m.ww(PLAYER + TIMER, 0); }
  m.wb(PLAYER + TENTHS, TENTHS_START);
  m.wb(PLAYER + TICKS, STOPPED);
  m.ww(GAME_STATE, f.fresh ? GAME_START : RACE_END);
  f.phase = phase(m);
  f.intro = 0;
}

}  // namespace marble_flow

#endif  // ENABLE_MARBLE
