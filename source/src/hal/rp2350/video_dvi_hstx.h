#ifndef HAL_RP2350_VIDEO_DVI_HSTX_H
#define HAL_RP2350_VIDEO_DVI_HSTX_H

// DVI (HDMI connector) over the RP2350's HSTX, 640x480 at 60 Hz. No
// framebuffer: submitted strips go to a ring, and the scanout IRQ builds
// each output line from it as the beam gets there (strip_ring.h). The
// scanout is the frame clock: endFrame() returns at the next vblank.
//
//   main task (video core)            DMA IRQ (video core)
//   renderRow -> submit() --ring-->   line build (dvi_line.h) -> HSTX
//   endFrame(): wait for vblank  <--  vblank
//
// SDK calls are out of line (video_dvi_hstx.cpp, see time.h).

#include <stdint.h>
#include "../../config.h"
#include "strip_ring.h"

namespace hal {

class DviHstxVideo {
public:
  // game pixels available: the whole screen at 1x, half of it at 2x
  // (rows scale 1.5x at least: 480 / 1.5 = 320)
  enum : uint16_t {
    CANVAS_W = DVI_SCALE == 1 ? 640 : 320,
    CANVAS_H = DVI_SCALE == 1 ? 480 : 320
  };

  void begin();
  void setViewport(uint16_t width) { viewport_width = width; }
  void flip(char flipY, char flipX);
  void flipReset(char, char) { flip(0, 0); }

  // the scanout runs at 60 Hz: always full rate
  void setHalfRate(bool) {}
  uint8_t screenParts() const { return 1; }

  // Scan order is top to bottom: a flipY screen renders its last strip first.
  uint8_t stripIndex(uint8_t i) const {
    return flip_y == dvi::Mirror::ON ? STRIPS - 1 - i : i;
  }

  // Copies the strip into the ring; waits while the ring is full.
  void submit(const uint16_t *strip, uint32_t pixels);
  // Returns at the next vblank; strips submitted after it form the next frame.
  void endFrame(uint8_t part, uint32_t frameStartUs);

private:
  static const uint8_t STRIPS = 36;
  static const uint8_t RING_SLOTS = 8;

  // DMA interrupt: queues the next part of the scanout
  static void irq();
  void buildLine(uint32_t line, uint32_t buffer);
  // restarts the scanout after the DMA chain lost its framing
  void resync();

  dvi::StripRing<RING_SLOTS> ring;
  volatile uint16_t viewport_width = 224;
  dvi::Mirror flip_x = dvi::Mirror::OFF;
  dvi::Mirror flip_y = dvi::Mirror::OFF;
  uint32_t frames_reported = 0;
};

} // namespace hal

#endif // HAL_RP2350_VIDEO_DVI_HSTX_H
