#ifndef HAL_HOST_VIDEO_NULL_H
#define HAL_HOST_VIDEO_NULL_H

#include <stdint.h>

namespace hal {

// Discards strips, never waits. Headless runs and host tests.
class NullVideo {
public:
  void begin() {}
  void setViewport(uint16_t) {}
  void flip(char, char) {}
  void flipReset(char, char) {}
  void setHalfRate(bool half) { half_rate = half; }
  uint8_t screenParts() const { return half_rate ? 2 : 1; }
  void submit(const uint16_t *, uint32_t pixelCount) {
    submit_count++;
    pixel_count += pixelCount;
  }
  void endFrame(uint8_t, uint32_t) { end_frame_count++; }

  // test access
  uint32_t submits() const { return submit_count; }
  uint32_t pixels() const { return pixel_count; }
  uint32_t endFrames() const { return end_frame_count; }

private:
  bool half_rate = false;
  uint32_t submit_count = 0;
  uint32_t pixel_count = 0;
  uint32_t end_frame_count = 0;
};

} // namespace hal

#endif // HAL_HOST_VIDEO_NULL_H
