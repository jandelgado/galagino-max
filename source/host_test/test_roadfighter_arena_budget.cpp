// roadfighter's live decompressed assets must fit Arena::CAPACITY, else
// Arena::alloc() aborts on machine start.
#include <cassert>
#include <cstdio>
#include "../src/emulation/asset.h"
#include "../src/emulation/arena.h"
#include "../src/machines/roadfighter/roadfighter_rom_main.h"
#include "../src/machines/roadfighter/roadfighter_rom_audio.h"
#include "../src/machines/roadfighter/roadfighter_tiles.h"
#include "../src/machines/roadfighter/roadfighter_sprites.h"

void test_roadfighter_assets_fit_arena_budget() {
  // Assets the ctor and render_row keep resident.
  uint32_t total = 0;
  total += roadfighter_rom_main_raw.size() * sizeof(unsigned char);
  total += roadfighter_rom_audio.size()    * sizeof(unsigned char);
  total += roadfighter_tiles.size()        * sizeof(uint32_t[8]);
  total += roadfighter_sprites.size()      * sizeof(uint32_t[32]);

  printf("roadfighter live asset budget: %u / %u bytes\n", total, (unsigned)Arena::CAPACITY);
  assert(total <= Arena::CAPACITY);
  printf("1. roadfighter assets fit within Arena::CAPACITY: OK\n");
}

int main() {
  test_roadfighter_assets_fit_arena_budget();
  printf("\nALL ROADFIGHTER ARENA BUDGET TESTS PASSED\n");
  return 0;
}
