#ifndef MARBLE_HISCORE_H
#define MARBLE_HISCORE_H

#include <stdint.h>

// High score table of the title (MARBLE.md "Attract"): ten entries in table order, outside
// the work RAM image (race loads clear that). RAM only, the defaults again after a power
// cycle. The video core reads it while the title shows; only the initials entry (Phase 6c)
// writes it, never while the title shows.
namespace marble_hiscore {

static const uint8_t ENTRIES = 10;

struct Entry {
  char initials[3];
  uint32_t score;
};

struct Table {
  Entry e[ENTRIES];
  uint8_t version;  // changes with every write (the title grid rebuilds)
  bool loaded;
};

// ROM defaults (0x1eea0) once: no-op when the table is loaded (start() runs twice)
void defaults(Table &t);

}  // namespace marble_hiscore

#endif
