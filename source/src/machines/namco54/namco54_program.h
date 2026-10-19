#ifndef NAMCO54_PROGRAM_H
#define NAMCO54_PROGRAM_H

#include <stdint.h>

// The 54xx program (1 KB, 54xx.bin in galaga.zip), shared by Galaga and
// Xevious. romconv writes namco54_rom.h here for either game (both scripts
// unpack galaga.zip; same bytes, last one wins). namco54_rom.h defines the
// bytes static, so including it in both machines would put two copies in
// flash: namco54_program.cpp is the one translation unit that includes it.
const uint8_t *namco54Program(void);

#endif
