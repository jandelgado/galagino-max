#ifndef HAL_RP2350_VIDEO_DVI_HSTX_H
#define HAL_RP2350_VIDEO_DVI_HSTX_H

// DVI (HDMI connector) over the RP2350's HSTX, 640x480 at 60 Hz. Submitted
// strips go to a 240 x 288 framebuffer; the scanout IRQ builds each output
// line from it (dvi_line.h). A TV has no picture memory of its own: without
// the framebuffer every strip would have to beat the beam, and slow frames
// (many sprites, half rate machines) showed black lines. The scanout is the
// frame clock, the render follows the beam one pass behind: submit()
// overwrites a strip once the current pass has read it, the next pass shows
// it, so a slow render does not tear (half rate: a second half that loses
// the race to row 144 still mixes two screens, see endFrame).
//
//   main task (video core)                DMA IRQ (emulation core)
//   renderRow -> submit() --framebuffer--> line build -> HSTX
//   submit(): wait for the beam      <--  beam position (rows read)
//   endFrame(): wait for strip 0     <--  wake as strip 0 / last row is read
//
// With HAL_AUDIO_DVI the stream is HDMI: every line carries a data island
// (hdmi_line.h), the IRQ sends audio packets from audio_dvi.h's ring.
//
// SDK calls are out of line (video_dvi_hstx.cpp, see time.h).

#include <stdint.h>
#include "../../config.h"
#include "dvi_line.h"

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

  // Half rate: a screen is rendered in two parts, one per scanout, as on
  // the SPI panel; the scanout stays at 60 Hz.
  void setHalfRate(bool half) { half_rate = half; }
  uint8_t screenParts() const { return half_rate ? 2 : 1; }

  // Scan order is top to bottom: a flipY screen renders its last strip first,
  // so rendering follows the beam.
  uint8_t stripIndex(uint8_t i) const {
    return flip_y == dvi::Mirror::ON ? STRIPS - 1 - i : i;
  }

  // Copies the strip into the framebuffer once the current pass has read
  // its rows.
  void submit(const uint16_t *strip, uint32_t pixels);
  // Returns once the next pass has read strip 0 (half rate, first part: once
  // this pass has read every row); after the last part the next strip is the
  // top one again.
  void endFrame(uint8_t part, uint32_t frameStartUs);

private:
  static const uint8_t STRIPS = 36;

  // DMA interrupt: queues the next part of the scanout
  static void irq();
  static void scanout();
  // task on the emulation core: the IRQs run there
  static void enableIrqs(void *);
  void buildLine(uint32_t line, uint32_t buffer);
  // restarts the scanout after the DMA chain lost its framing
  void resync();

  volatile uint16_t viewport_width = 224;
  dvi::Mirror flip_x = dvi::Mirror::OFF;
  dvi::Mirror flip_y = dvi::Mirror::OFF;
  bool half_rate = false;
  uint8_t written = 0;  // strips of the current screen
};

} // namespace hal

#endif // HAL_RP2350_VIDEO_DVI_HSTX_H
