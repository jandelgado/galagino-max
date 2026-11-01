#include "../../catalog.h"
#if defined(GAME_GALAGA) || defined(GAME_SXEVIOUS) || defined(GAME_XEVIOUS)

#include "namco54_program.h"
#include "namco54_rom.h"
#include "../../emulation/arena.h"

const uint8_t *namco54Program(void) {
  const Asset<unsigned char, COMPRESSED> rom(namco54_rom_blob);
  uint8_t *program = Arena::alloc<uint8_t>(rom.size());
  rom.decodeInto(program);
  return program;
}

#endif // namco54 games
