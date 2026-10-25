#ifndef HAL_FRAME_PACER_H
#define HAL_FRAME_PACER_H

#include <stdint.h>

namespace hal {

// Deadline pacing of full-rate screens at 60.0 Hz: waits alternate 16/17/17
// ms. A fixed 16 ms pad ran at 62.5 Hz, games 4% fast, and beat against the
// 60 Hz panel: a skipped frame (scroll jerk) about every second. Pure, so the
// host tests it; the video backend sleeps the returned time.
class FramePacer {
public:
  static const int32_t FRAME_US = 16667;
  // Later than this: restart from now, no catch-up burst.
  static const int32_t MAX_LATE_US = 50000;
  // No full-rate frame for this long (half-rate machine, boot): the old
  // deadline is stale, beyond 2^31 us it even reads as far ahead.
  static const uint32_t MAX_PAUSE_US = 1000000;

  // Microseconds from nowUs to the next frame's deadline; <= 0: start now.
  int32_t next(uint32_t nowUs) {
    const uint32_t pause = nowUs - lastUs;
    lastUs = nowUs;
    if (pause > MAX_PAUSE_US) {
      nextUs = nowUs;
      return 0;
    }

    nextUs += FRAME_US;
    const int32_t wait = (int32_t)(nextUs - nowUs);
    if (wait < -MAX_LATE_US) {
      nextUs = nowUs;
    }
    return wait;
  }

private:
  uint32_t nextUs = 0;
  uint32_t lastUs = 0;  // time of the previous next() call
};

} // namespace hal

#endif // HAL_FRAME_PACER_H
