#ifndef TP84_HW_H
#define TP84_HW_H

#include <cstdint>
#include <cstring>

// Time Pilot '84 sprite multiplexing. The sub CPU reads the beam line and
// rewrites sprite slots mid-screen; MAME redraws up to the beam on each
// write (update_partial). SpriteBands keeps each slot's content per range
// of native lines, so render can clip every version to its lines.
namespace tp84_hw {

constexpr uint8_t SLOTS = 24;         // sprite RAM 0x67A0-0x67FF
constexpr uint8_t VIS_TOP = 16;       // first visible native line
constexpr uint16_t VIS_END = 240;     // one past last visible line (= vblank)
constexpr uint8_t MAX_SPRITES = 48;   // 24 slots + 24 mid-screen versions (max 7 seen)

// One slot's content over native lines [top, end).
struct Sprite {
  uint8_t x, code, attr, sy;   // raw sprite RAM bytes 0..3
  uint8_t top, end;
  uint8_t slot;
};

struct SpriteBands {
  uint8_t ram[SLOTS * 4];      // live sprite RAM (sub CPU reads/writes here)

  void reset() {
    memset(ram, 0, sizeof(ram));
    beginFrame();
  }

  // All slots valid from VIS_TOP, no retired entries.
  void beginFrame() {
    memset(start, VIS_TOP, sizeof(start));
    retired_n = 0;
  }

  // A visible-line change retires the slot's content for [start, line]:
  // MAME update_partial(vpos) draws the write line with the old content.
  // Retire list full: old content is lost, current content never.
  void write(uint8_t off, uint8_t val, uint8_t line) {
    if (ram[off] == val) return;
    const uint8_t slot = off >> 2;
    if (line >= VIS_TOP && line < VIS_END) {
      const uint8_t from = line + 1;    // first line with the new content, <= VIS_END
      if (from > start[slot] && retired_n < MAX_SPRITES - SLOTS)
        retired[retired_n++] = make(slot, start[slot], from);
      start[slot] = from;
    }
    ram[off] = val;
  }

  // Draw order (slot 23 first, slot 0 on top); out[MAX_SPRITES].
  uint8_t finish(Sprite *out) const {
    uint8_t n = 0;
    for (int slot = SLOTS - 1; slot >= 0; slot--) {
      for (uint8_t i = 0; i < retired_n; i++)
        if (retired[i].slot == slot) out[n++] = retired[i];
      out[n++] = make(slot, start[slot], VIS_END);
    }
    return n;
  }

private:
  uint8_t start[SLOTS];
  Sprite retired[MAX_SPRITES - SLOTS];
  uint8_t retired_n;

  Sprite make(uint8_t slot, uint8_t top, uint16_t end) const {
    const uint8_t *s = &ram[slot * 4];
    return Sprite{s[0], s[1], s[2], s[3], top, (uint8_t)end, slot};
  }
};

}  // namespace tp84_hw

#endif
