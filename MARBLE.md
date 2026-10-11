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
- Descriptor +0x04: row offset table (slapstic, word per row), +0x10: camera y at the top
  of the race, +0x12: camera range in px (360, 920, 840, 784, 896, 624), +0x2a: attribute
  index table (slapstic, byte per row). Race height = range + one 240 px screen: 75, 145,
  135, 128, 142, 108 rows (range / 8 alone misses the goal area).
- Row decode 0x1a668 (Marble Love decode-bitstream-1a668.ts): 36 words per row from a
  bitstream at 0x800e4 + signed row offset; codes 14 bit literal, 7 bit runs (+1 run,
  table 0x249da run, 0x4d/0x4e alternation), 9 bit table 0x2499a; attribute high bytes from
  run-length pairs (count, high byte) at 0x2be18 + index byte. Word = tile low bits + high
  byte << 8: bit 15 hflip, bits 8-14 lookup PROM index (bits 12-13 pick the colour set).
  No race uses the hflip bit (bit 15) and bit 14 is always 0 in decoded races; runtime
  patches (0x1344c, Phase 4) may still use flip.
- Row n uses offset word +0x04[n] and attribute byte +0x2a[n], no other input. Callers:
  level init 0x16f6c (32 rows from row 0 into 0xa00006, race 4 from the bottom) and
  scroll redraw 0x13ee6 (one row per 8 px of camera, row (camera - +0x10) / 8 - 1, +0x20
  when scrolling down). decode_level() matches MAME playfield RAM for every row of all 6
  races (tools/pf_check.py). Reading the attribute table at +0x2a + 0x4e gives wrong
  colour sets (practice row 9 grey band).
- Camera: 0x40097c (long, +0x10 .. +0x10 + +0x12), playfield y scroll 0x400000. Race
  index: 0x400394 (word, 0-5).
- 0x13ee6 has a second path for objects of type 0x19: it decodes a row of ultimate
  (descriptor 0x2be14) with attribute byte +0x2a + 0x4e + row into an object buffer.
  Probable source of the +0x4e offset (open, Phase 4).
- 0x1a444 (Marble Love tilemap-row-build-1a444.ts) does not touch colours: it packs
  terrain data into off-screen playfield RAM columns 39-63 (row offsets 0x1eb3a,
  0x4e + 0x80 * row) (Marble Love source, not verified).
- Scroll draws rows past a race's last row: they come from the next descriptor's tables
  in the same bank (aerial shows silly rows 0-1 off screen).
- Slapstic bank per level (found by decoding all banks): 1, 2, 2, 3, 3, 0. Switching
  sequence not traced (Marble Love slapstic-103.ts).
- Unique playfield tiles per level (decode_level + pf_lookup codes): 760, 896, 729, 683,
  866, 595; 3726 over all levels.
- Palette sets: 6 x 195 words at 0x1fc10, written to the addresses listed at 0x20534 by
  0x26b2a; set n belongs to level n (verified visually).
- Animated level pieces are blitted by 0x1344c from a list at RAM 0x400970 (open).
- Surface/height lookup reads the playfield word and indexes a table at RAM 0x40065a
  (0x177f8) (open, Phase 2).

## MAME playfield oracle

`romconv/internal/marble/tools/mame_pf.lua` dumps playfield RAM every 30 frames; without
`MARBLE_LEVEL` it records attract mode. With `MARBLE_LEVEL=n` it inserts a coin, starts,
forces race n (write tap on 0x400394) and drives the camera with a read tap on the
marble screen y (0x400038: 0x00 = scroll down, 0xf0 = scroll up), so every row is drawn
by the game. `tools/pf_check.py PFSEQ.BIN` matches RAM rows to decode_level() and exits 1
on any difference.

```sh
MARBLE_OUT=$PWD MARBLE_LEVEL=2 MARBLE_FRAMES=2200 SDL_VIDEODRIVER=dummy \
  SDL_AUDIODRIVER=dummy mame marble -rompath romszip -video none -sound none -nothrottle \
  -noautosave -skip_gameinfo -snapshot_directory "$PWD" -cfg_directory "$PWD/cfg" \
  -nvram_directory "$PWD/nv" -autoboot_script /abs/path/mame_pf.lua
python3 romconv/internal/marble/tools/pf_check.py pfseq.bin
```

Seen rows per race (2200 frames): 70/75, 140/145, 129/135, 122/128, 142/142, 107/108,
0 mismatches. Unseen rows have tile words shared with other rows (blank rows, repeated
bottom rows), so pf_check cannot identify them.

## Open questions

- Phase 2 trap: the surface lookup at 0x177f8 indexes by the original playfield word (ROM
  tile code / PROM index). The converted map keeps only local tile index, colour set and
  flip, and is written on the video core. Phase 2 needs the original words or a per-local-tile
  surface table on the emulation core.
- Terrain/height data format (descriptor +0x00, +0x08, +0x1c), Phase 2.
- Motion object format and playfield/MO priority (atarisy1_v.cpp), Phase 2.
- Object/enemy scripts, Phase 4. Sound command protocol, Phase 5.
