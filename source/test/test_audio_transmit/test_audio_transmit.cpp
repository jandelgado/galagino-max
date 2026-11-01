#include <unity.h>

// Audio (portable mixer) against the host sink: volume math, mute, the
// all-or-nothing chunk write and silence while no machine is running.
// Runs on the host HAL backend (HAL_HOST).
#include "../../src/emulation/audio.cpp"

extern "C" {
void ResetZ80(Z80 *) {}  // machineBase::reset() is in the vtable, never called
}

namespace {
class Ramp : public machineBase {
public:
  int16_t level = 100;
  void renderAudio(int16_t *out, int n) override {
    for (int i = 0; i < n; i++) {
      out[i] = level;
    }
  }
};

int captured;
int16_t chunks[8][AUDIO_CHUNK];

void capture(const int16_t *samples, uint32_t n) {
  if (captured < 8) {
    memcpy(chunks[captured], samples, n * sizeof(int16_t));
  }
  captured++;
}
}

void setUp(void) {
  captured = 0;
  hal::host::setAudioHook(capture);
  hal::host::setAudioCapacity(0xFFFFFFFFu);
}
void tearDown(void) {}

// volume 3 (default): value * 64 / 3
static void test_volume_scaling(void) {
  Audio audio;
  Ramp m;
  audio.init();
  audio.start(&m);
  hal::host::setAudioCapacity(2);
  audio.transmit();
  // first write is the pending silence chunk, then one rendered chunk is
  // pending and the second write sends it
  TEST_ASSERT_EQUAL_INT(2, captured);
  TEST_ASSERT_EQUAL_INT16(0, chunks[0][0]);
  TEST_ASSERT_EQUAL_INT16(100 * 64 / 3, chunks[1][0]);
  TEST_ASSERT_EQUAL_INT16(100 * 64 / 3, chunks[1][AUDIO_CHUNK - 1]);
}

static void test_mute_sends_silence(void) {
  Audio audio;
  Ramp m;
  audio.init();
  audio.start(&m);
  audio.mute(1);
  hal::host::setAudioCapacity(2);
  audio.transmit();
  TEST_ASSERT_EQUAL_INT16(0, chunks[1][0]);
}

// transmit() sends as many chunks as the sink takes and stops at the first
// refusal, rendering one chunk per accepted write
static void test_stops_when_sink_full(void) {
  Audio audio;
  Ramp m;
  audio.init();
  audio.start(&m);
  hal::host::setAudioCapacity(3);
  audio.transmit();
  TEST_ASSERT_EQUAL_INT(3, captured);
  hal::host::setAudioCapacity(0);
  audio.transmit();
  TEST_ASSERT_EQUAL_INT(3, captured);
}

// no machine: pending stays silent, the sink is still fed
static void test_no_machine_is_silent(void) {
  Audio audio;
  audio.init();
  hal::host::setAudioCapacity(4);
  audio.transmit();
  TEST_ASSERT_EQUAL_INT(4, captured);
  for (int c = 0; c < 4; c++) {
    TEST_ASSERT_EQUAL_INT16(0, chunks[c][10]);
  }
}

// stop() drops the machine and flushes the sink with silence
static void test_stop_flushes_silence(void) {
  Audio audio;
  Ramp m;
  audio.init();
  audio.start(&m);
  hal::host::setAudioCapacity(2);
  audio.transmit();
  captured = 0;
  audio.stop();
  TEST_ASSERT_TRUE(captured >= 1);
  TEST_ASSERT_EQUAL_INT16(0, chunks[0][0]);
  hal::host::setAudioCapacity(2);
  captured = 0;
  audio.transmit();
  TEST_ASSERT_EQUAL_INT16(0, chunks[1][0]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_volume_scaling);
  RUN_TEST(test_mute_sends_silence);
  RUN_TEST(test_stops_when_sink_full);
  RUN_TEST(test_no_machine_is_silent);
  RUN_TEST(test_stop_flushes_silence);
  return UNITY_END();
}
