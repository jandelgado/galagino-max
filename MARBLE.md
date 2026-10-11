# Marble Madness (Atari 1984) reverse engineering notes

Basis for a galagino remake: graphics, levels and music come from the ROM, the logic is
reimplemented in C++ (no 68010 emulation). Plan: docs/superpowers/plans/2026-10-11-marble.md.
Reference: Marble Love (github.com/magno73/marble-love, MIT), a function-by-function
TypeScript port named by ROM address. Facts below were read from the disassembly
(capstone, romconv/internal/marble/tools) unless marked otherwise.

## ROM (MAME `marble`, set 1, atari/atarisy1.cpp)

- 68010 7.159 MHz program 0x10000-0x2ffff (8 x 16 KB, even/odd interleaved), slapstic 103
  bank ROM 0x80000-0x87fff (136033.107/108, 4 x 8 KB banks, window 8 KB mirrored).
  Cartridge calls BIOS routines at 0x100-0x254 (not needed by the remake).
- 6502 1.79 MHz sound program $8000-$ffff (136033.421/422), YM2151 3.58 MHz, POKEY 1.79 MHz.
- Graphics (ROMREGION_INVERT): playfield = bank 1, 5 planes x 32 KB, 4096 8x8 tiles at
  5 bpp (lookup PROM 136033.118/.119, all playfield entries bank 1, 5 bpp). Motion objects
  = bank 2 tiles 2048-4095 (149/151/153), 4 bpp with plane 3 empty.
- Screen 336x240 ROT0 (set_raw 7.159 MHz, 456x262). The playfield used by races is 36
  tiles = 288 px wide, drawn from playfield RAM column 3 (0xa00006).
- Palette: IRGB_4444, 1024 entries. Playfield colours 0x200 + colour_set * 32 + pixel,
  motion objects 0x100 + set * 16 + pixel (verified visually for playfield only).

## Levels

- Level table 0x2be00: 6 x long -> descriptor (practice 0x2bee2, beginner 0x2c54c,
  intermediate 0x2cd9e, aerial 0x2d648, silly 0x2de1e, ultimate 0x2e790).
  Field meanings: Marble Love docs/level-header-format.md.
- Descriptor +0x04: row offset table (slapstic), +0x12: height in px (360, 920, 840, 784,
  896, 624), +0x2a: per-row attribute index (slapstic), + 0x4e.
- Row decode 0x1a668 (Marble Love decode-bitstream-1a668.ts): 36 words per row from a
  bitstream at 0x800e4 + signed row offset; codes 14 bit literal, 7 bit runs (+1 run,
  table 0x249da run, 0x4d/0x4e alternation), 9 bit table 0x2499a; attribute high bytes from
  run-length pairs at 0x2be18 + index. Full-screen init: 0x16f80.
- Slapstic bank per level (found by decoding all banks): 1, 2, 2, 3, 3, 0. Switching
  sequence not traced (Marble Love slapstic-103.ts).
- Unique playfield tiles per level: 551, 673, 514, 591, 818, 395; 3009 over all levels.
- Palette sets: 6 x 195 words at 0x1fc10, written to the addresses listed at 0x20534 by
  0x26b2a; set n belongs to level n (verified visually).
- Animated level pieces are blitted by 0x1344c from a list at RAM 0x400970 (open).
- Surface/height lookup reads the playfield word and indexes a table at RAM 0x40065a
  (0x177f8) (open, Phase 2).

## Open questions

- Terrain/height data format (descriptor +0x00, +0x08, +0x1c), Phase 2.
- Motion object format and playfield/MO priority (atarisy1_v.cpp), Phase 2.
- Object/enemy scripts, Phase 4. Sound command protocol, Phase 5.
