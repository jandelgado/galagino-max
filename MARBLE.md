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
- 0x1a444 does not touch colours: it builds the terrain cells (section Terrain) into
  off-screen playfield RAM columns 39-63 and motion object RAM 0xa02400+ (verified).
- Scroll draws rows past a race's last row: they come from the next descriptor's tables
  in the same bank (aerial shows silly rows 0-1 off screen).
- Slapstic bank per level (found by decoding all banks): 1, 2, 2, 3, 3, 0. Switching
  sequence not traced (Marble Love slapstic-103.ts).
- Unique playfield tiles per level (decode_level + pf_lookup codes): 760, 896, 729, 683,
  866, 595; 3726 over all levels.
- Palette sets: 6 x 195 words at 0x1fc10, written to the addresses listed at 0x20534 by
  0x26b2a; set n belongs to level n (verified visually).
- Animated level pieces are blitted by 0x1344c from a list at RAM 0x400970 (open).
- Surface/height lookup does not use the visible playfield words: it reads the terrain
  cells built by 0x1a444 (section Terrain).

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

## Logic tick

- IRQ4 vblank handler 0x28788 runs at 60 Hz: palette animation, sound, timers, trackball
  read 0x1ac18 (0x28824). The game logic main loop 0x117b2 waits for two vblanks
  (0x118ba, 0x118c8): 30 Hz. MAME traces: player ticks always 2 frames apart during a race
  (2400+ ticks, 3 traces).
- Per logic tick, object dispatcher 0x251de calls 0x253ec per object: switch on state
  +0x1a (jump table 0x254ba). State 0 (rolling) 0x256d2: 0x253bc (save last ground),
  0x17f66 (input, 0x26196 speed cap), 0x121b8 (physics). State 1 (dizzy) 0x2574c: same
  plus animation 0x25fc2 and a countdown after 0x121b8.
- 0x1ac18 stores only the latest frame's counter delta (+0xc6/+0xc7), so the tick sees
  every second frame's trackball motion.

## Marble physics

Player object 0x400018 (player 2 0x4000fa, stride 0xe2). 16.16 fixed point, pixels.

| off | size | meaning |
| --- | --- | --- |
| +0x00/04/08 | long | vx, vy, vz (per tick) |
| +0x0c/10/14 | long | x, y, z. z high word = height, ground near 0x4000 |
| +0x18 | byte | slot in use (1), 2/3 = out of play |
| +0x1a | byte | state: 0 roll, 1 dizzy after landing, 2 broken, 4 lost (respawn), 6 goal |
| +0x1b, +0x1d | byte | zone (checkpoint area), zone at last ground |
| +0x1e/+0x20 | word | screen x, screen y (0x1b9cc), +0x22..+0x29 two older pairs |
| +0x2a | long | z at last ground (fall height) |
| +0x32/+0x34 | word | last ground cell (x >> 19, y >> 19) |
| +0x36 | byte | 0 on ground, 1 teetering at an edge, 2 airborne |
| +0x37 | byte | edge direction mask while teetering |
| +0x56/+0x57 | byte | dizzy counter, accumulated fall damage |
| +0x58/+0x59 | byte | surface code set by level object slots (0x29cce), timer |
| +0xa4 | 4 x 6 | marble surface dots for the sprite (0x1c014) |
| +0xc6/+0xc7 | byte | trackball delta port 2 / port 0 (0x1ac18) |

Tick (0x121b8, ported 1:1 in tools/marble_sim.py `physics_121b8`):

1. Input 0x17f66: vx += s8(+0xc7) * 0x160, vy -= s8(+0xc6) * 0x160 (state 1: scaled by
   0x1f - +0x56). Only on ground or edge (+0x36 != 2) and surface code in {0, 0x2d-0x31,
   0x38-0x3b}. Airborne: vz -= 0x6000, min -0x50000.
2. Speed cap 0x261bc: n = max(|vx|,|vy|) + 3/8 min (mulu), cap 0x40000 (state 0) or
   0x50000; above: v = (v >> 8) * ((cap << 6) / (n >> 8)) >> 6.
3. 0x121b8: terrain under the old position (0x1bab2, 0x1cc62); more than 16 px below the
   terrain = embedded, lost. x += vx, y += vy, z += vz.
4. Side bounds: sx = y - x + 0x88 (word); sx < 4 and vx > vy, or sx > 0x11c and vy > vx:
   swap vx/vy, restore x/y and move both by (vx + vy) / 2 (0x12886).
5. Edges 0x1c676: compares the 16 quadrant heights of the 4 cell vertices, gives 4 side
   and 4 diagonal wall heights relative to z (0x400674-0x400683) and flags 0x40066a-
   0x400672. Quirk: a side output is not written when its wall test is skipped, the stale
   value (from the previous 0x121b8 call, any object) still gets z subtracted. The port
   must keep these 8 words across ticks.
6. Walls 0x1b5c2: walls >= 4 px reflect vx/vy (abs or negate) and restore x or y
   (sound 0x42). Level object slots 0x29cce (below) can do the same.
7. Zone lines 0x1365c (player): crossing a line sets +0x1b; zone 0xff = goal: state 6,
   v = (s8 0x1eb2e[race], s8 0x1eb34[race]) << 16 (practice stops if zone was 3/4).
8. Height h = 0x1cc62 at the new position. Edge 0x160f6: a lower neighbour edge starts
   teetering (+0x36 = 1, vz = -0x6000), z more than 6 px above h starts a fall (+0x36 =
   2). |z - z_old| > 8 px with vz <= 0 also starts a fall (sound 0x45).
9. Airborne: h >= z lands: z = h, vz = 0, fall = |+0x2a - h| >> 15 (half pixels) to
   0x25c74: state 1, +0x57 += fall (max 0x7f), +0x56 + (+0x57 - +0x56) / 2 > 0x1f breaks
   the marble (state 2). Falling more than 128 px below +0x2a (0x800000) = lost.
   On ground: z = h.
10. Edge push 0x1706c: next to a lower edge (0x1c676 code 1-2, output > 0), v += table
    0x23d40[fraction in the cell] toward the edge.
11. On ground only: friction 0x25e7c by surface code (0 normal; 2, 3, 4 other modes; none
    for 0x10, 0x17, 0x18, 0x32-0x37), then gravity 0x25df6: g = gradient word per cell
    (0x4006a4/6, height delta over 8 px), |g| >= 12 multiplies by 4, v -= g << 11
    (silly race 4: v += g << 11).
12. State 1 countdown (0x25776): +0x57 -= 1; if +0x57 < +0x56: +0x56 -= 1, at 0 back to
    state 0, else +0x56 += 1.

Friction 0x25e7c: n as in the speed cap, f = table 0x1eef8 interpolated by n bits 12-18,
mode 0: k = (n - f) * 64 / (n >> 8), v = (v >> 8) * k >> 6 (n at least 0x100).

Level object slots 0x400a9c (25 x 0x56, +0x18 active, +0x0c/+0x10 position, +0x1f tag)
run byte code scripts (0x12896) spawned by level object lists (Phase 4). 0x29cce clears
+0x58 and tests every slot (jump table 0x29db4): tags 0x1a-0x1f are invisible wall
segments (pixel boxes relative to the slot, end caps reflect the other axis), tags 0x10,
0x17, 0x18, 0x2d-0x3b are cell rectangles that set +0x58 (0x2d-0x31, 0x38-0x3b also need
equal z). Practice uses tags 0x11 (no-op) and 0x1a-0x1f. Tags 0x05, 0x0a-0x0d,
0x12-0x16, 0x20-0x27 (bumpers, gates, pipes, catapult) are not ported (Phase 4).

Validation (tools/marble_sim.py, traces from tools/mame_trace.lua, random stick input,
3000 frames): `ticks` from each MAME snapshot and `run` free running from the first
snapshot, terrain from ROM, slots/dynamic codes/enemy scratch copied from the trace.
Position and velocity match exactly (0 px) for every tick except deaths and enemy
contacts: practice 3 traces, 2987 ticks, longest free run 738; beginner 3 traces, 3100
ticks, longest 634; intermediate, silly, ultimate exact; aerial diverges only where
unported slot tags 0x0b/0x0d act. Enemy contacts (object pair collision 0x1bc88) are not
ported.

```sh
# trace: race 0, stick script "frames:port0 delta:port2 delta" per 60 Hz frame from 1000
MARBLE_OUT=$PWD MARBLE_LEVEL=0 MARBLE_FRAMES=3000 MARBLE_INPUT_FRAME=1000 \
  MARBLE_INPUT=60:4:0,60:0:-6 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy mame marble \
  -rompath romszip -video none -sound none -nothrottle -noautosave -skip_gameinfo \
  -cfg_directory "$PWD/cfg" -nvram_directory "$PWD/nv" -autoboot_script /abs/mame_trace.lua
python3 romconv/internal/marble/tools/marble_sim.py run trace.bin 0   # also: ticks, terrain
```

## Terrain

Height field on an iso vertex grid, independent of the playfield graphics. Built once per
race by 0x1a444 at level init (0x16f6c), read by 0x1caba.

- Vertex (X, Y) in cells lies on diagonal d = X + Y - 0x15, column c = 0x15 - X + d / 2
  (0x1caba). Diagonals 0 .. descriptor +0x18 - 1 (160, 216, 192, 216, 238, 200), 22
  columns (odd diagonals 21).
- Each vertex has 4 heights, one per quadrant: q0 (-x,+y), q1 (+x,+y), q2 (+x,-y),
  q3 (-x,-y). Cell (cx, cy): 0x1caba loads vertices (cx,cy+1), (cx+1,cy+1), (cx+1,cy),
  (cx,cy) to 0x401c28 (steps table 0x24b3a); 0x1cc62 takes q2, q3, q0, q1 of them as the
  cell corner heights h(0,8), h(8,8), h(8,0), h(0,0). Height inside the cell: two
  triangles split at fy >= fx, h = h00 + dx * fx + dy * fy (16.16 result, dx/dy are the
  gradient used by gravity). Height 0 = no floor.
- Storage: 10 bit vertex index into the code table (descriptor +0x26, +0x24 entries),
  pairs of diagonals packed MSB first at 0xa00000 + word 0x1eb3a[d / 2] (pair rows
  0-62 at playfield columns 39-63, rows 63+ at 0xa02400 + 0x36 * n), field = column, +22
  for odd diagonals (0x1a9cc, 54 bytes). Read back by 0x1caba via table 0x1ed0a.
- Code word (0x1cb64), base = per diagonal word (descriptor +0x0c, RLE count/value words,
  0x18fd0, 0x4000 on the course):
  - 0x0000 no floor.
  - 0x0001-0x07ff: offset of a 4 byte record at descriptor +0x00; byte per quadrant,
    0 = no floor, else base - 0x80 + byte.
  - 0x0800-0x0fff: dynamic, word at RAM 0x40076e + (code & 0x7fe) (moving parts, Phase 4).
  - 0x1000-0xefff: v = base + (code & 0x7f) - 0x40; quadrant q has v if bit 12 + q is set,
    else v - table 0x1ed62[(code >> 7) & 0x1f] (0x1000 = no floor).
  - 0xf000-0xffff: flat v.
- Build 0x1a444, chunks of 24 diagonals: rectangles 0x1ad54 (descriptor +0x1c, +0x1a
  entries of 8 bytes: x0 s8, y0, x size s8, y size, base height word, b6 pattern index
  0-4 + flags, b7 fill direction 0-2 (table 0x1ecea) + bit 3 subtract), one enable bit
  per rectangle per chunk (descriptor +0x08 words), then single vertex overrides from the
  same stream (word cell, word code; 0xfffe next chunk, 0xffff end). A rectangle writes
  base +/- pattern byte (descriptor +0x20 pattern pointers, signed bytes, 0x80 repeats) to
  the quadrants inside it. 0x1aa38 encodes each vertex to a code (flat, two level, or a
  record searched at descriptor +0x00) and 0x1abd4 binary searches the code table.
- Slapstic: terrain data lives in bank 0x239a0[race] (0, 1, 1, 1, 1, 1). Rasterising
  starts in bank 0x24994[race] (2, 2, 1, 2, 0, 0); after every rectangle 0x2bc5c pokes
  the slapstic from the rectangle's bits (x0 bit 0, sizes bit 2, base bits 1-3, b6 bits
  5-7; 0x2ff40 alt sequence with the 68010 prefetch at 0x2ff5a as alt1, Marble Love
  docs/findings), so the next rectangle reads its pattern from another bank. Modelled in
  marble_sim.py `slapstic_2bc5c`.
- Verified: marble_sim.py terrain build equals MAME video RAM for all pairs of all 6
  races (`marble_sim.py terrain`).
- Start (0x259b4): descriptor +0x14 + 2 * player, high byte cell x, low byte cell y,
  x = cell * 8 + 4, z = terrain height. Respawn (0x2591a, 0x2637a): checkpoint list
  0x1ef1a[race] (cx, cy, zone, pad; 0xff end), nearest checkpoint of zone +0x1d to the
  last ground cell (distance max + 3/8 min of the cell deltas * 16 < 0x300) that no other
  object blocks (0x17cb8), else the last entry of zone - 1. Trace: lost at cell (39, 24),
  respawned at checkpoint (35, 24).
- Zone lines 0x1eb16[race]: 6 bytes (x, y, end, zone a, zone b, 0); x < 0: line y = |y|
  from x = |x| to end, else line x from y = |y| to end; leaving a line cell sets the zone
  of the side (0x1365c). Zone 0xff = goal.

## Marble sprite

- Projection 0x1b9cc: sx = y - x + 0x88, sy = camera + z + 0x54 - (x + y) / 2 (words,
  camera = 0x40097e), sy counts up from the bottom of the 240 line screen (start sy 184
  near the top; camera rule below). Surface codes 0x2f-0x31 add 2 on odd x at speed
  (bumps). Stored at +0x1e/+0x20.
- Motion object RAM 0xa02000 (bank 0, 64 entries, 4 word planes of 0x80 bytes): word 0
  y << 5 | hflip 0x8000 | height - 1, word 1 code, word 2 x << 5 | priority 0x8000,
  word 3 link (MAME atarisy1_v.cpp). MO x = sx + 16, MO y = sy + 16 (traces).
- The player marble is 7 MOs (traces): codes 0x0005 and 0x0003 (8 x 16 halves), four
  8 x 8 surface dots (codes 0x10-0x2c by angle, positions from 0x1c014 rotating 4 points
  by the velocity; angle table 0x24ade per Marble Love, unverified), code 0x0007 5 lines
  lower (shadow or highlight, unverified). Code to tile/colour via the PROM lookup (ROM).
- Occlusion: the playfield priority mask 0x840000 is written 0 once at boot (0x100b6,
  MAME write tap during a race: no other write), so the playfield never covers motion
  objects by hardware. MAME merge: priority MOs (word 2 bit 15) replace the pixel with
  palette 0x300 + pf pen * 16 + mo pen. Level objects use such MOs (codes 0x4e0-0x571, 7
  to 8 tiles tall, seen in traces); hypothesis (unverified): they mask the marble behind
  walls. Spawned by level object scripts (Phase 4).

## Camera

- 0x13ee6 (main loop via 0x10fce), every 8th frame or every frame while a scroll is
  running (0x400008). d3 = lowest sy of the active players (silly race: inverted).
- Thresholds (0x1f1ca..0x1f1d6): sy < 0x48 scrolls forward (camera +), sy > 0xa8 back;
  speed per call 4 when beyond the threshold by more than 0x30, 2 by more than 0x10, else
  the previous speed; a running scroll continues until sy is back inside 0x58-0x98.
  Camera clamped to descriptor +0x10 .. +0x10 + +0x12; playfield y scroll 0x400000 moves
  by the same step (mod 0x200). Traces: camera steps +1/+2 per tick, only while
  sy <= 0x58.
  Exact speed hysteresis not trace-verified.

## Open questions

- Level object slots: spawn lists and byte code (0x12896), unported slot tags, dynamic
  terrain codes 0x40076e, priority mask MOs. Phase 4.
- Enemy collision 0x1bc88 and enemy updates of the shared scratch words. Phase 4.
- Death/respawn timing (states 2/4, 0x13ade), sprite dots 0x1c014 and MO emission
  0x264aa (not ported, formats above).
- Object/enemy scripts, Phase 4. Sound command protocol, Phase 5.
