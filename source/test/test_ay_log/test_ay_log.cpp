// AyLog: timestamped AY register writes, replayed in 8-sample slices.
#include <functional>
#include <unity.h>

#include "../../src/emulation/ay_log.h"

void setUp(void) {}
void tearDown(void) {}

static const int REGS = 16;
static const int SLICE = AyLog<REGS>::SLICE_SAMPLES;
static const int FRAME = AyLog<REGS>::FRAME_SAMPLES;

// records reg 0 as seen by each rendered slice
struct Capture {
  uint8_t seen[FRAME * 3 / SLICE];
  uint8_t last = 0;
  int slices = 0;

  void operator()(const uint8_t *regs, int16_t *out, int n) {
    TEST_ASSERT_EQUAL(SLICE, n);
    if (slices < (int)sizeof(seen)) {
      seen[slices] = regs[0];
    }
    last = regs[0];
    slices++;
  }
};

static void playSamples(AyLog<REGS> &log, Capture &cap, int samples) {
  int16_t out[64];
  for (int done = 0; done < samples; done += 64) {
    log.play(out, std::min(64, samples - done), std::ref(cap));
  }
}

// a write 100 samples into the frame is not heard before slice 12
static void test_write_lands_in_its_slice(void) {
  AyLog<REGS> log;
  log.beginFrame(1000);
  log.at(250);  // 250 / 1000 * 400 = sample 100
  log.write(0, 7);
  log.endFrame();

  Capture cap;
  playSamples(log, cap, FRAME);
  TEST_ASSERT_EQUAL(FRAME / SLICE, cap.slices);
  TEST_ASSERT_EQUAL(0, cap.seen[11]);
  TEST_ASSERT_EQUAL(7, cap.seen[12]);
  TEST_ASSERT_EQUAL(7, cap.seen[FRAME / SLICE - 1]);
}

// two writes inside one frame are both heard, in order
static void test_fast_writes_are_not_lost(void) {
  AyLog<REGS> log;
  log.beginFrame(1000);
  for (int i = 0; i < 40; i++) {
    log.at(i * 25);  // every 10 samples
    log.write(0, i + 1);
  }
  log.endFrame();

  Capture cap;
  playSamples(log, cap, FRAME);
  int changes = 0;
  for (int s = 1; s < cap.slices; s++) {
    changes += cap.seen[s] != cap.seen[s - 1];
  }
  TEST_ASSERT_GREATER_OR_EQUAL(30, changes);
}

// without a published frame the last register values hold, no stall
static void test_empty_log_holds_registers(void) {
  AyLog<REGS> log;
  log.beginFrame(1000);
  log.at(0);
  log.write(0, 9);
  log.endFrame();

  Capture cap;
  playSamples(log, cap, 3 * FRAME);
  TEST_ASSERT_EQUAL(9, cap.seen[cap.slices - 1]);
}

// the next frame is not replayed before the previous one has played out
static void test_frames_play_in_sequence(void) {
  AyLog<REGS> log;
  log.beginFrame(1000);
  log.at(0);
  log.write(0, 1);
  log.endFrame();
  log.beginFrame(1000);
  log.at(0);
  log.write(0, 2);
  log.endFrame();

  Capture cap;
  playSamples(log, cap, 2 * FRAME);
  TEST_ASSERT_EQUAL(1, cap.seen[FRAME / SLICE - 1]);
  TEST_ASSERT_EQUAL(2, cap.seen[FRAME / SLICE]);
}

// a full ring drops writes but keeps the frame markers
static void test_overflow_keeps_frames(void) {
  AyLog<REGS> log;
  for (int f = 0; f < 4; f++) {
    log.beginFrame(1000);
    for (int i = 0; i < 2000; i++) {
      log.at(i / 2);
      log.write(0, f + 1);
    }
    log.endFrame();
  }

  Capture cap;
  playSamples(log, cap, 8 * FRAME);
  TEST_ASSERT_TRUE(cap.slices > 0);
}

// An emulation clock a little faster than the audio clock must not build up
// latency until the ring overflows and silence writes are lost: the audio
// side skips frames, keeping their register writes.
static void test_lead_is_bounded(void) {
  AyLog<REGS> log;
  const int FRAMES = 40;
  for (int f = 0; f < FRAMES; f++) {
    log.beginFrame(1000);
    log.at(0);
    log.write(0, f + 1);
    log.endFrame();
  }

  Capture cap;
  playSamples(log, cap, 2 * FRAME);
  TEST_ASSERT_GREATER_OR_EQUAL(FRAMES - 4, cap.seen[cap.slices - 1]);
}

// the emulation runs 4 frames per 3 played, 100 writes each: the final
// state arrives and nothing was dropped on the way
static void test_fast_emulation_keeps_final_state(void) {
  AyLog<REGS> log;
  Capture cap;
  int produced = 0;
  for (int f = 0; f < 120; f++) {
    log.beginFrame(1000);
    for (int i = 0; i < 100; i++) {
      log.at(i * 10);
      log.write(0, (f + i) & 0x7f);
    }
    log.at(999);
    log.write(0, f == 119 ? 0 : 1);
    log.endFrame();
    produced++;
    if (produced % 4 != 0) {
      playSamples(log, cap, FRAME);
    }
  }
  playSamples(log, cap, 3 * FRAME);
  TEST_ASSERT_EQUAL(0, cap.last);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_write_lands_in_its_slice);
  RUN_TEST(test_fast_writes_are_not_lost);
  RUN_TEST(test_empty_log_holds_registers);
  RUN_TEST(test_frames_play_in_sequence);
  RUN_TEST(test_overflow_keeps_frames);
  RUN_TEST(test_lead_is_bounded);
  RUN_TEST(test_fast_emulation_keeps_final_state);
  return UNITY_END();
}
