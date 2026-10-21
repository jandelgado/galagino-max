#include "marble_hiscore.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

#include <string.h>
#include "marble_rom.h"

namespace marble_hiscore {

void defaults(Table &t) {
  if (t.loaded) { return; }

  for (uint8_t k = 0; k < ENTRIES; k++) {
    memcpy(t.e[k].initials, marble_hiscore_defaults[k].initials, sizeof(t.e[k].initials));
    t.e[k].score = marble_hiscore_defaults[k].score;
  }
  t.version++;
  t.loaded = true;
}

}  // namespace marble_hiscore

#endif  // GAME_MARBLE
