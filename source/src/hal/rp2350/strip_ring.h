#ifndef HAL_RP2350_STRIP_RING_H
#define HAL_RP2350_STRIP_RING_H

// Ring of rendered strips between the frame loop (writer) and the DVI
// scanout IRQ (reader), in place of a framebuffer. The writer renders a
// frame top to bottom while the beam shows it; the ring only has to hold the
// strips between the beam and the writer.
//
//   scan frame N:  beam -> row 100            writer -> strip 20
//   slots:  [strip 12][13]...[19][20 being written]
//            ^ released once the beam passed all rows of strip 12
//
// Writer and reader run on the same core (the IRQ preempts the writer),
// so volatile fields and compiler barriers suffice. No SDK dependencies.

#include <stdint.h>
#include "dvi_line.h"

namespace hal {
namespace dvi {

template <uint8_t SLOTS>
class StripRing {
public:
  static const uint16_t WIDTH = 240;
  static const uint16_t STRIP_LINES = 8;
  static const uint32_t STRIP_PIXELS = WIDTH * STRIP_LINES;

  // writer: slot of the next strip, null while every slot holds a strip
  // the beam still needs. A writer behind the scanout never waits.
  uint16_t *tryAcquire() {
    if (writeFrame == scanFrame && (uint16_t)(written - released) >= SLOTS) {
      return nullptr;
    }
    return slots[written % SLOTS];
  }

  // writer: the acquired slot is complete
  void publish() {
    barrier();
    written = written + 1;
  }

  // writer, after a vblank: the next strips belong to the frame being scanned
  void startWrite() {
    written = 0;
    barrier();
    writeFrame = scanFrame;
  }

  // reader, at vblank: a new frame starts at row 0
  void startScan() {
    released = 0;
    barrier();
    scanFrame = scanFrame + 1;
  }

  // reader: source row of output row `scanRow`, null if not rendered
  const uint16_t *row(uint16_t scanRow, Mirror flipY) const {
    const uint16_t strip = scanRow / STRIP_LINES;
    if (writeFrame != scanFrame) {
      return nullptr;
    }
    if (strip >= written) {
      late = late + 1;
      return nullptr;
    }

    const uint16_t line = flipY == Mirror::ON ? STRIP_LINES - 1 - scanRow % STRIP_LINES
                                              : scanRow % STRIP_LINES;
    return slots[strip % SLOTS] + line * WIDTH;
  }

  // reader: strips wholly above `firstRowNeeded` may be reused
  void release(uint16_t firstRowNeeded) { released = firstRowNeeded / STRIP_LINES; }

  uint32_t lateLines() const { return late; }
  void clearLate() { late = 0; }

private:
  static void barrier() { __asm volatile("" ::: "memory"); }

  uint16_t slots[SLOTS][STRIP_PIXELS];
  volatile uint32_t scanFrame = 0;
  volatile uint32_t writeFrame = ~0u;
  volatile uint16_t written = 0;
  volatile uint16_t released = 0;
  mutable volatile uint32_t late = 0;
};

} // namespace dvi
} // namespace hal

#endif // HAL_RP2350_STRIP_RING_H
