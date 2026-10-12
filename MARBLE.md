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
- 0x1344c (called by 0x13ee6) has a second path for level object tag 0x19 (ultimate moving
  parts): it decodes ultimate rows (descriptor 0x2be14) with attribute byte +0x2a + 0x4e +
  row into playfield RAM, row index from the animation record (section Level objects).
  Source of the +0x4e offset (row decode not ported).
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
- Animated level pieces are blitted by 0x1344c from the animation frame record queued at
  RAM 0x400970 (section Level objects).
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
- 0x400010 counts logic ticks, not frames: the IRQ adds 1 (0x287a6) only when the main loop
  set 0x40039a (0x118c0, before its last wait; cleared 0x287bc). Traces: + 1 per tick.
- Per logic tick, object dispatcher 0x251de calls 0x253ec per object: switch on state
  +0x1a (jump table 0x254ba). State 0 (rolling) 0x256d2: 0x253bc (save last ground),
  0x17f66 (input, 0x26196 speed cap), 0x121b8 (physics). State 1 (dizzy) 0x2574c: same
  plus animation 0x25fc2 and a countdown after 0x121b8.
- Main loop body 0x11804-0x118ce: 0x4003f0 + 1, 0x1101e (race: 0x10fce = 0x13ee6 (0x1344c,
  camera, 0x144e4), players 0x251de, 0x189e2, enemies 0x158cc, 0x1493c, 0x4003f0 + 1,
  0x17230, 0x1912c, 0x19baa, 0x1844a, slots 0x12fd0, 0x4003f0 + 1, HUD 0x28624), countdowns
  0x400768/0x4003b8, rng(0x100) 0x118a4 (result unused), MO list 0x26f3e, two vblank waits
  0x28dea (0x4003f0 + 1 each; the first is skipped after a lag frame). Traces: 0x4003f0 + 5
  and 0x400010 + 1 per tick.
- 0x1ac18 stores only the latest frame's counter delta (+0xc6/+0xc7), so the tick sees
  every second frame's trackball motion.
- Trackball hardware (MAME trakball_r): port 0xf20000 = x + y, 0xf20002 = x - y of the
  ball; 0x1ac18 stores port 0 in +0xc7, port 2 in +0xc6.
- Remake (marble.cpp): run_frame() runs one logic tick every second frame, 0x400010 + 1
  first (rule above): queued animation frame 0x1344c (`applyQueued`), camera 0x13ee6 with
  the band spawns 0x144e4 (`cameraBands`, when the camera moved), `marble_physics::tick`
  (0x251de: deltas to +0xc6/+0xc7, state handler 0x253ec; the physics tail runs the slot
  contacts 0x29cce, the projection 0x1b9cc and the occlusion masks 0x1281c), the slot update
  0x12fd0 (`objects::update`), rng(0x100) 0x118a4 (`marble_rng::rng`), the MO list (sort,
  build), then publish. Order from the disassembly 0x117b2-0x118ce; Marble Love main-tick.ts
  approximates the main loop (rng(0x100) on its wait tick). Not modelled: race timer and
  other IRQ work, countdowns 0x400768/0x4003b8. Stick as trackball, deltas per tick, k =
  `MARBLE_STICK_K` (9, about MAME's keyboard trackball speed; tune on the device): right c7
  = c6 = -k, left +k, +k, down c7 = +k, c6 = -k, up c7 = -k, c6 = +k; two directions add,
  scaled by 181/256 (about 1.4k on the axis both use). EXTRA held: no input (EXTRA + UP/DOWN
  is volume). FIRE: next race (debug until the game flow phase): cleared work RAM image,
  race start, camera and pan reset.

## Marble physics

Player object 0x400018 (player 2 0x4000fa, stride 0xe2). 16.16 fixed point, pixels.

| off | size | meaning |
| --- | --- | --- |
| +0x00/04/08 | long | vx, vy, vz (per tick) |
| +0x0c/10/14 | long | x, y, z. z high word = height, ground near 0x4000 |
| +0x18 | byte | slot in use (1), 2/3 = out of play |
| +0x1a | byte | state: 0 roll, 1 dizzy after landing, 2 broken, 4 lost (respawn), 6 goal; enemies: 5 eaten, 7 hidden, 9 dissolving, 0xb carried; 3 catapult, 8 time out, 0xa gate |
| +0x1b, +0x1d | byte | zone (checkpoint area), zone at last ground |
| +0x1e/+0x20 | word | screen x, screen y (0x1b9cc), +0x22..+0x29 two older pairs |
| +0x2a | long | z at last ground (fall height) |
| +0x32/+0x34 | word | last ground cell (x >> 19, y >> 19) |
| +0x36 | byte | 0 on ground, 1 teetering at an edge, 2 airborne |
| +0x37 | byte | edge direction mask while teetering |
| +0x56/+0x57 | byte | dizzy counter, accumulated fall damage (states 4/0xb: sparkle ticks) |
| +0x58/+0x59 | byte | surface code set by level object slots (0x29cce), timer |
| +0x5a, +0x5f, +0x60 | long, byte, byte | animation list pointer (frame header longs), tick counter, ticks per frame |
| +0x62, +0x66, +0x67 | long, byte, byte | shatter overlay list pointer, counter, on |
| +0x6a-+0x6f | 3 words | race time: seconds, tenths, frames (vblank IRQ) |
| +0x74/+0x84/+0x94 | 8 words each | surface dot points x, y, z (0x4000 = 1, 0x1c014) |
| +0xa4 | 4 x 6 | marble surface dots for the sprite (0x1c014), state 4 sparkle stars 0-3 |
| +0xbc | long | score; +0xc4 word: distance score sum (0x26214) |
| +0xc6/+0xc7 | byte | trackball delta port 2 / port 0 (0x1ac18) |
| +0xca, +0xd2 | byte, word | dot point renormalised next (0-7), death count |

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
run byte code scripts (0x12896) spawned by level object lists (section Level objects). 0x29cce clears
+0x58 and tests every slot (jump table 0x29db4): tags 0x1a-0x1f are invisible wall
segments (pixel boxes relative to the slot, end caps reflect the other axis), tags 0x10,
0x17, 0x18, 0x2d-0x3b are cell rectangles that set +0x58 (0x2d-0x31, 0x38-0x3b also need
equal z). Practice uses tags 0x11 (no-op) and 0x1a-0x1f. Tag 0x05 is ported from Marble
Love (no trace contact). Tags 0x0a-0x0d, 0x12-0x16, 0x20-0x27 (gates, pipes, catapult) are
not ported (section Level objects).

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

### Player states, deaths, RNG

Ported in `marble_sim.py` with `Mem.deaths` (`main_tick`, `object_253ec`; Marble Love
refresh-frame-10fce.ts, object-state-entry-25bae.ts, helper-25fc2.ts, object-init-2591a.ts,
object-orbit-emit-13ade.ts, sprite-rotate-1c014.ts). Handlers of 0x253ec (table 0x254ba):

- 0 roll (0x256d2): +0xcb set (time out event 0x1bbaa) and on ground: state 8 (not ported);
  else 0x253bc, 0x17f66, 0x121b8.
- 1 dizzy (0x2574c): 0x25fc2, 0x253bc, 0x17f66, 0x121b8, countdown (step 12 above); at 0
  state 0, +0x5a = 0, dots 0x1c014.
- 2 shatter (0x25824): 0x25fc2, projection 0x1b9cc (cell refetch), masks 0x1281c if +0x1c.
- 4 lost (0x255c6): projection, sparkle 0x13ade; at its end (or +0x57 < 2) 0x17cb8 (radius
  0x70) checks the spot again: taken: 0x25bae(4) (new spot), else v = 0, state 0.

Entries and steps:

- Landing 0x25c74 (player): state 1, +0x57 += fall (max 0x7f). Break when +0x56 + (+0x57 -
  +0x56) / 2 > 0x1f: on screen (-0x10 < sy < 0xf0) 0x25bae(2), else +0x57 = 0x64,
  0x25bae(4). No break and not dizzy before: +0x5a = 0x20faa, +0x60 = 2.
- 0x121b8 embedded (+0x57 = 0x65) and fall beyond 128 px (+0x57 = 0x64): 0x25bae(4), exit.
- 0x25bae(code): vx = vy = 0. 2: +0x5a = 0x20fde, +0x60 = 2, +0x56 = 2. 9: 0x21062, +0x60 =
  4. 4: respawn 0x2591a at once (the sparkle shows the new spot), +0x5a = 0, +0xd2 + 1.
- 0x25fc2: next frame every +0x60 ticks (counter +0x5f). Shatter frame 9 starts the overlay
  +0x62 = 0x215f6 (+0x67), stepped every tick after. List end (0xffffffff): states 1/5 loop
  0x20fb6 (+0x56 > 6) or 0x20fd2; state 2 (+0x56 = 2): +0x67 = 0, +0xa4 = 0, +0x57 = 0x65,
  0x25bae(4) (+0x56 0/1: dizzy end list, shatter again; not used by players); others: lost.
- Respawn 0x2591a: point 0x262b2, x/y, z = terrain, zone, v, +0x36, +0x56, +0x58,
  +0x22-+0x29 cleared, dot pose 0x25b40 (0x1d3f4/0x1d3fc), projection, zone hook 0x13966
  (silly, aerial only). 0x262b2/0x2637a as in section Terrain; every candidate first runs
  0x17cb8 (radius 0x180) and is skipped when taken.
- 0x17cb8 (0x17cb8-0x17f64): players (not itself), steelies 0x4009a4, slinkies 0x401302 with
  +0x18 set, both pixel deltas below 32 and octant distance (deltas * 16) below the radius:
  returns 1/2/4, index to 0x40046a.
- Sparkle 0x13ade: +0x57 0x64 -> 48 ticks, 0x65 -> 24, 0x66 (bird) -> 36. 8 stars (MO code
  0x10b + k, height 1, no mask bit) on a circle of radius +0x57 / 2 at +0x1e/+0x20, angle +10
  of 0x192 per tick (sine 0x1eda2, offsets 0x1ef32); visible ones (-8 < x < 0x120, -8 < y <
  0xf0) to +0xa4 (first 4), then +0x38; +0x1c = 1.
- Dots 0x1c014 (players in the 0x121b8 tail unless state 1/5, dizzy end): 8 points turned by
  (vx, vy) >> 4, one of points 0/1 renormalised per tick in turn (+0xca), points 2-7 mirror
  them; sorted by screen x, pairs give 4 dot MOs at +0xa4 (code 0x24ade, y bias 0x24b2c).
- Distance score 0x26214 (in the speed cap 0x261bc, players): +0xc4 += friction table
  0x1eef8 at the speed >> 9; above 0xa0: - 0xa0, +0xbc += 10.
- Timing (traces): dizzy 44-48 ticks; shatter 64 ticks (32 frames) + sparkle 24 = control
  88 ticks after the landing; fall: sparkle 48 ticks after the fell tick.
- Player MOs (0x26f3e type 1, `marble_mo.type1`, equal to MO RAM in all death ticks):
  states 1/2/5/9 draw frame rl(+0x5a) (set +0x19) instead of halves, dots and shade; +0x67
  adds rl(+0x62) set 2; state 4 draws the stars (+0xa4, +0x38); masks +0x38 last. Frame
  headers: dizzy 0x20faa (0x21386, 0x21392), 0x20fb6 (0x2139e-0x213da, 6), 0x20fd2
  (0x213e6, 0x213f2); shatter 0x20fde (0x213fe-0x2146a, 10, then 0x2137a); overlay 0x215f6
  entries 0-43 (0x214ca-0x2157e, 16, and 0x2137a); dissolve 0x21062 (0x21476-0x214be, 7).

RNG 0x13a98 (word 0x4003a6): rng(0x100) per tick at 0x118a4 after 0x1101e (slots 0x12fd0
included); race start 0x10fb8 rng(0x100) -> 0x400444 (time out event: below
0x24a94[race], 0x1bbaa; level init also sets 0x40076c = 1); other consumers: section
Enemies.

Goal 6 (0x254d2, Marble Love refresh-frame-10fce.ts JT[6], `object_253ec`): physics, +0x57
(0x1e from 0x1365c) - 1; at 0 and +0x18 != 2: +0x18 = 3 (out of play, no input), v = 0.
+0x57 wraps to 0xff, so this repeats every 256 ticks while state 6 lasts (the game flow
ends the race first; not modelled). None of the recorded MAME traces reaches state 6.

Race start: `start_259b4` / `start_race` end at a `main_tick` boundary (the first tick runs
0x253bc/0x17f66); `tick()` users add `snapshot_start`. `begin_race` = the remake's
beginRace (start_race, projection, masks).

Remake (`marble_physics.cpp`, `marble_rng.cpp`): handlers of states 0, 1, 2, 4, 6, 0x25bae
(2, 4), 0x25fc2, 0x2591a, 0x262b2/0x2637a with 0x17cb8, 0x13ade, 0x26214, race start rng.
Not ported: dots 0x1c014/0x25b40, zone hook 0x13966, time out 0x1bbaa (state 8; needs the
race timer), +0xd8 wobble, sounds. Host test `test_marble_deaths`: the death fixture per
tick (player fields, +0x5a, frame headers, masks, RNG; score, distance, death count at
segment ends, from the traces); race start RNG; start fixture `marble_start_trace.h`
(`make_fixture.py rwrite`, model only): per race beginRace + 60 ticks of stick input, and a
goal crossing (marble on the first goal line, 300 idle ticks: goal, out of play, one +0x57
wrap), crc32 of player 1 (dots +0x74-+0xbb, +0xca zeroed) and 0x4003a6 per tick; every race
kills the marble through the machine's start and tick path (200 px fall and 20 px embedded:
lost, sparkle; 40 px drop: shatter, sparkle) and rolls again within 300 ticks; a race switch
while dead starts clean.

```sh
python3 romconv/internal/marble/tools/make_fixture.py rwrite \
  source/test/test_marble_deaths/marble_start_trace.h
```

Validation: `deaths` runs free between teleports of tick start traces (`MARBLE_TICK=start`);
per tick the player object (but the IRQ bytes +0x6a-+0x6f, +0xc6-+0xc9), 0x4003a6, slots,
dynamic codes and player MOs equal MAME. pd1 (practice, `MARBLE_PLACE`
800:140:140:22,1000:140:140:70,1500:164:308:-21, input from 1100
350:1:0,150:0:-4,800:0:1): 34, 98 (dizzy), 248 (shatter, sparkle 24), 448 ticks (fall,
sparkle 48). bd2 (beginner, 800:700:812:-256:11,1400:676:780:-186:11,1900:628:772:-256:11,
2500:676:780:-232:11, input from 1550 300:1:0,150:0:4,450:1:-1,800:1:0): 48, 248 (shatter),
298 (fall off row 94), 349 ticks (dizzy). Not covered: practice landing markers (type 0x2a,
0x12532), 2 player respawn (0x2523e), states 3, 5, 7-0xb.

```sh
MARBLE_TICK=start ... mame_trace.lua            # one snapshot per main loop tick
python3 romconv/internal/marble/tools/marble_sim.py deaths pd1/trace.bin 0 \
  0:798 800:998 1000:1498 1500:2400             # FROM:TO segments between teleports
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
    Slots and diagonals per race: practice 0-8 (diagonals 11-16), beginner 0-11 (96-103),
    intermediate 0-65 (137-162), ultimate 0-111 (144-189); aerial, silly none. Level
    object animations rewrite them while the race runs (section Level objects).
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
  respawned at checkpoint (35, 24). Details: distance per checkpoint = |dx| << 4 and
  |dy| << 4 (words), max + (min >> 3) * 3, the smallest below 0x300 wins (0x400462/
  0x400466 pixel = cell * 8 + 4, 0x400472 zone); the fallback scans backwards from the
  0xff end entry for zone +0x1d - 1 (zone 0 stays 0). 0x2591a then sets x/y, z = terrain,
  +0x1b = that zone, clears velocity, +0x36, +0x56, +0x58; 0x25c34 calls it when the
  marble is lost, then state 4.
- Remake port: `marble_physics` 0x2591a, 0x262b2, 0x2637a with 0x17cb8 (section Player
  states, deaths, RNG). Without 0x17cb8 (Phase 3) it equalled MAME in 22 of 23 trace
  respawns; the miss (beginner, cell (26, 33), MAME (22, 36)) fits a blocked spot. No
  checkpoint found: pixel (0, 0), zone 0xff, as the arcade.
- Zone lines 0x1eb16[race]: 6 bytes (x, y, end, zone a, zone b, 0); x < 0: line y = |y|
  from x = |x| to end, else line x from y = |y| to end; leaving a line cell sets the zone
  of the side (0x1365c). Zone 0xff = goal.

## Marble sprite

- Projection 0x1b9cc: sx = y - x + 0x88, sy = camera + z + 0x54 - (x + y) / 2 (words,
  camera = 0x40097e; x + y summed as longs, asr 1), sy counts up from the bottom of the
  240 line screen (start sy 184 near the top; camera rule below). Surface codes 0x2f-0x31
  add 2 to z on odd x when |vx| > 0x4000 (bumps). Stored at +0x1e/+0x20. Verified against
  the traces (tools/mame_trace.lua, 10 traces): +0x20 equals the formula with the previous
  tick's camera in 9547 of 9640 ticks, the rest differ by 1-3 px in ticks where code after
  0x1b9cc moved the marble (mostly surface 0x2d).
- Motion object RAM 0xa02000 (bank 0, 64 entries, 4 word planes of 0x80 bytes): word 0
  y << 5 | hflip 0x8000 | height - 1, word 1 code, word 2 x << 5 | priority 0x8000,
  word 3 link (MAME atarisy1_v.cpp). MO x = sx + 16, MO y = sy + 16 (traces).
- The player marble is 7 MOs (traces): codes 0x0005 and 0x0003 (8 x 16 halves), four
  8 x 8 surface dots (codes 0x10-0x2c by angle, positions from 0x1c014 rotating 4 points
  by the velocity; angle table 0x24ade per Marble Love, unverified), code 0x0007 at MO y
  + 5 (shadow or highlight, unverified). Code to tile/colour via the PROM lookup (ROM).
- Screen position (MAME atarimo, MO yscroll 256, no MO x scroll; playfield x scroll 0
  from the vblank IRQ 0x287cc, y scroll = 0x400000): MO screen x = MO x, MO top line =
  (-(MO y) - 256 - height * 8) & 0x1ff. Playfield: screen line L shows race pixel row
  camera - descriptor +0x10 + L, screen x X shows race x X - 24 (playfield column 3).
  So in race pixels the 0x05 half has its top left at (sx - 8, camera - +0x10 + 224 - sy),
  0x03 at x + 8, 0x07 at MO y + 5: MO y counts up the screen, the top line also moves by
  the height (1 tile vs 2), so its top is 3 screen lines below the 0x05 top (227 - sy). Link order 0x05, 0x03, dots,
  0x07; MAME draws in link order, later on top. Calibrated with MAME snapshots of
  practice (frames 900, 1300, 1700, camera 0 and 40 px down): playfield matches the
  decoded race at that offset exactly, marble pixels inside the predicted 16 x 16 box; the
  remake's frame at race start equals the MAME frame pixel for pixel apart from the HUD
  and the surface dots.
- Occlusion: the playfield priority mask 0x840000 is written 0 once at boot (0x100b6,
  MAME write tap during a race: no other write), so the playfield never covers motion
  objects by hardware. The marble hides behind walls through its own priority mask MOs,
  computed per tick from the terrain (section Occlusion), not through level objects
  (correction: the earlier hypothesis was wrong).

## Level objects

Static and scripted level pieces (start walls, gates, tubes, moving floors, animated
playfield patches) are slots, not tiles. Sources: ROM disassembly and Marble Love
(script-slot-step-13068.ts, helper-12896.ts, sub-29cce.ts, scroll-range-144e4.ts,
terrain-wave-update-1d06a.ts); ported in tools/marble_sim.py and validated against MAME
traces (below). Enemies (0x4009a4, 0x401302, ...) are other arrays and not covered.

Tick order (main loop 0x10fce, 30 Hz): 0x13ee6 (first 0x1344c, then camera; a camera
change calls 0x144e4 = spawns) -> 0x251de players and other objects (the player physics
0x121b8 reads the slots via 0x29cce) -> 0x189e2, 0x158cc, 0x1493c, 0x17230, 0x1912c,
0x19baa, 0x1844a -> 0x12fd0 slot update -> 0x28624. So physics sees the slot state and
dynamic codes of the previous tick; a slot's animation frame is applied by 0x1344c at the
start of the next tick. The MO list (0x26f3e, main loop 0x118aa) shows the logic one tick
later.

Slot (0x400a9c + 0x56 * i, i = 0-24, allocated in index order via table 0x1f016):

| off | size | meaning |
| --- | --- | --- |
| +0x00/+0x04 | long | vx, vy (op 14: word << 8) |
| +0x0c/+0x10/+0x14 | long | x, y (cell << 19), z (word << 16) |
| +0x18 | byte | 1 active |
| +0x19 | byte | slot number i: draw list index (0x18e6c/0x18f46). Set once at boot by the slot array init (Marble Love slot-array-init.ts); the remake writes it at race start in `marble_objects::init` |
| +0x1a | byte | state: 0 wait, 1/2 animate, 3 start script, 4 wait for zone |
| +0x1b | byte | zone (op 17) |
| +0x1c | word | wait ticks / animation repeat count (0 = forever) |
| +0x1e | byte | mode: 0 motion object, 1 playfield patch, 2 MO + terrain |
| +0x1f | byte | tag (0x29cce collision kind) |
| +0x20/21, +0x22/23 | byte | frame counter / ticks per frame, state 1 and 2 |
| +0x24/+0x25 | byte | loop counters (ops 4/5, 6/7); tag 6: wave phase |
| +0x26/+0x28 | word | playfield patch column, race row (mode 1) |
| +0x2a/+0x2e/+0x32 | long | loop pcs, saved pc (op 10/17) |
| +0x36 | long | script pc |
| +0x3a | long | spawn script |
| +0x3e | long | current animation record; +0x46 (state 2) / +0x4a (state 1) list starts |
| +0x42 | long | MO frame record (modes 0/2) |
| +0x4e | long | screen sx << 16 \| sy (same projection as 0x1b9cc) |
| +0x52/+0x54 | word | camera band range of the spawn entry |

Spawning (0x144e4 -> 0x12dfa): band = (camera - descriptor +0x10) >> 4 (16 px). List
0x1dec0[race]: 6 byte entries (lo band, hi band, long script; 0xff end); script 0 = a
random group of 4 scripts from 0x1ded8 + 16 * rng(4) (0x13a98, shift register 0x4003a6,
aerial only). When the camera moves from band old to new: every entry with new == lo or
hi and old outside [lo, hi] spawns (unless already alive: 0x12dae, same +0x3a) into the
first free slot (+0x3a = script, state 3, +0x52/+0x54 = lo/hi); then every slot with
old == +0x52 and new < +0x52, or old == +0x54 and new > +0x54 is removed (0x12f44 mode
1). Level init (0x105a0) spawns with old = camera -/+ 0x19 (silly: +), runs one 0x12fd0 and
0x1344c; slots do not run before the first race tick (sim `objects_init` equals the first
race snapshot of all 6 races).

Update 0x12fd0: intermediate only: a player in zone 9/10 with 0x40075e set spawns the
wave script 0x1d854; while a wave runs (0x40075c) 0x11ac2 reloads the 66 dynamic codes
from 0x1d370 each tick; then 0x13068 per slot: tag 3 steps three global animation
pointers (0x40044a/e/52); state 3 starts the script; state 0 counts +0x1c down then
continues the script; state 1 moves by v and advances the animation every +0x21 ticks,
state 2 the same without moving every +0x23 ticks (tag 6 counts +0x25, 0x1e sets
0x40075e; tag 0x19 sets 0x40045c = frame * 0x90); animation end (0xffffffff) wraps to the
list start, +0x1c counts repeats (0 = forever), at 0 the script continues. 0x13334 per
animated frame: modes 1/2 queue the record at 0x400970 (one per tick, last slot wins),
modes 0/2 store the screen position +0x4e and +0x42; tag 6 calls the wave 0x1d06a
(rewrites up to 12 dynamic codes from tables 0x24c7a-0x24e1a), tag 3 0x26b66.

Byte code 0x12896 (word opcode, args words; ops yield where marked):

| op | args | effect |
| --- | --- | --- |
| 0 | x, y, z, mode, tag [, col, row, long anim] | init (cells, z word); mode 1: patch at col/row, anim list; else MO draw list insert 0x18e6c |
| 1/2 | ticks, frames per step | animate list +0x4a / +0x46 (state 1 / 2), yield |
| 3 | ticks | wait (0 = forever), yield |
| 4/6, 5/7 | count | loop start / end (count 0 = forever) |
| 8 | 0, long | player within 3 cells of the slot: jump (sound 0x31) |
| 9, 10, 11 | long / long / - | jump, call, return |
| 12/13 | long | animation list for state 2 / 1 |
| 14 | vx, vy | velocity << 8 |
| 15 | - | remove slot, yield |
| 16 | - | x += vx, y += vy |
| 17 | zone | state 4: wait until a player enters the zone (0x1365c -> 0x13a1c), yield |
| 18 | long | jump when no player is in zone +0x1b (or +0x1b + 1 >= 0x1e) |

Animation records: mode 0 lists hold longs (MO frame for 0x1a8d2), modes 1/2 pairs of
longs (tile block or MO frame, terrain block). 0x1344c applies the queued record: mode 1
tile block = bytes w, h, then w x h playfield words written at column +0x26, race row
+0x28 (rows on screen only; the remake must patch its playfield copy); tag 0x19 decodes
ultimate rows instead (row index in the record); terrain block = word first slot, word
count, words to 0x40076e.

Objects per race (tools/objects_data.py):

| race | spawned tags | dynamic codes from |
| --- | --- | --- |
| practice | 0x11 start gate at (16,16) (mode 1: playfield patch + terrain; 150 ticks after the start, if a player is in cells 16-18 / 16-18, i.e. the start pen, it opens: lists 0x16658, 0x1668c, then the slot is removed), walls 0x1a-0x1d (start pen), 0x1e/0x1f, 0x05 | tag 0x11 frames |
| beginner | 0x03 (61,56) mode 2 moving part, tubes 0x12-0x16, 0x10, 0x05 | tag 0x03 lists 0x21156, 0x21192 |
| intermediate | tubes 0x20/0x21, 0x05, wave 0x06 (zone 9/10) | level init 0x11ac2 copy, wave 0x1d06a |
| aerial | 0x17/0x18 start surfaces, gates 0x0b/0x0d (zone scripts), 0x0a catapult, random 0x0c groups, 0x05 | none |
| silly | tubes 0x22-0x24, 0x05 | none |
| ultimate | 0x19 (mode 1, rows), surfaces 0x2d-0x3a, tubes 0x25-0x27, 0x05 | tag 0x19 list 0x24e2a |

Tags in 0x29cce (Marble Love sub-29cce.ts, tag dispatch 0x29db4): 0x05 round post
(max + 3/8 min of |dx| * 16, |dy| * 16 below 0x38, about 3.5 px: both axes reflect,
sound 0x42), 0x0a catapult, 0x0b/0x0d
aerial gates (may set state 4), 0x0c bounce box, 0x10/0x17/0x18/0x2d-0x3b surface cells
(+0x58), 0x12-0x16 and 0x20-0x27 tubes (teleport to fixed exits, state 3, blocked check
0x17cb8, shape test 0x1cd00, rng; some return without the bounce epilogue), 0x1a-0x1f
walls. Ported: 0x05, surfaces, walls.

Validation (`marble_sim.py slots TRACE RACE`: slots, dynamic codes and slot globals never
copied from the trace after the first race snapshot; camera long and enemy scratch are):
all slot fields and all 128 dynamic words equal MAME every tick, and the marble physics on
them, in 20 traces, 16757 ticks: practice 6 (3 random input, 2 start pen input, 1 camera
driven; 13 slot wall contacts, physics segments up to 934 ticks), beginner 3 + 1 camera
driven, intermediate 3 + 1 camera driven (physics segment 528), silly 2, ultimate 2 + 1
camera driven, aerial 1 short (camera traces: `MARBLE_CAMERA=800:1600` in
mame_trace.lua, spawns and removals over the whole race, beginner 46 and ultimate 37
dynamic code changes). The second aerial trace differs only where gates 0x0b/0x0d act.
Not covered by a trace: the intermediate wave (tag 6), zone triggers, tube tags.

## Occlusion

The marble (and every other visible object) gets up to 5 priority mask MOs per tick from
0x264aa, called by 0x1281c after the projection 0x1b9cc in the physics tail (0x1272c).
Ported 1:1 as `occlusion_264aa` (Marble Love fun-264aa.ts; ROM 0x2651c compares +0x1a == 9
where the TS reads +0x18).

- 0x1281c: +0x1c = 1 if -16 < sy < 256, then 0x264aa (players kind 0, others 1).
- Kind 0 skip: surface code +0x58 set with timer +0x59 != 0xff (and not 0x0a, state not
  9/0xa/0xb): +0x1c = 0, no masks; states 4, 6, 0xa: no masks.
- Columns: c = sx >> 3; 3 columns c-1..c+1 (states 1/2/5/9: 5 columns, c-2..c+2),
  clipped to 0..35. Sprite box top = sy + 0xe (0x20 for 5 columns). Diagonal per column =
  cell diagonal (cx + cy - 0x15) + offset byte from 0x1eaba (+10 when the pixel-sum
  diagonal differs, +5 when column and diagonal parity differ, +1 for 3 columns).
- Per column, first explicit occluders 0x1ea2a[col] (word offset per race, records of 5
  words: diagonal limit, depth, control, y, entry code; 175 records): control 0-7 full
  mask when y >= sy - control else emit, 8 emit when y < top, > 8 emit when y <= top else
  restart at diagonal control & 0xff, -2 full mask when the record z > marble z, -1 emit
  unless on the limit diagonal.
- Else terrain: first diagonal >= d with a bit in the column's edge bitmap (descriptor
  +0x2e + 4 * col (+ 0x40045c, ultimate), bit d & 7 of byte d >> 3; the remake uses edge
  lists, see Phase 3 data); the two vertex heights
  of that edge (0x177f8: terrain field of the column, higher of two quadrants picked by
  0x2417e and parity flag 0x400988) give screen lines y0, y1 relative to the sprite: both
  below the sprite: nothing; both above the top: full mask (entry 0x6ce0 at y = top -
  0x2f); else a slope mask, code 0x1ead2[|y1 - y0|] (+0x8000 hflip by direction), y =
  0x1eaf4[|y1 - y0|] + lower.
- All columns full (or columns 1-3 of 5): +0x1c = 0, the marble is not drawn.
- Entry (+0x38 + 6 * i): code, x = col * 8, y; first word 0 ends the list. Code bits: 15
  hflip, 14 always 1, 11-13 MO height - 1, 0-10 MO code (0x4e0-0x747, all pens 1). MO:
  x + 24, y + 16 (MO x/y words like the marble: marble MO x = sx + 16), priority bit,
  linked right after the object's own MOs (traces).
- MAME merge (atarisy1_v.cpp): a priority MO pixel with pen 1 keeps the playfield pixel;
  MOs drawn before it in link order are gone there. Mask tiles have only pens 0/1, so
  palette 0x300+ is never used and the rule is: a mask pixel clears the marble pixel.

Validation: `marble_sim.py occl TRACE RACE` (projection + masks of every tick from a MAME
snapshot): 10249 ticks of 14 traces, all races, 1903 with masks, 394 with the marble
hidden, 0 differ (one aerial tick where the respawn sparkle 0x13ade wrote +0x38 is
skipped). `tools/marble_occlusion.py` renders MAME frames (mame_trace.lua `MARBLE_SHOTS`)
from playfield + MO RAM with the MAME merge and with the remake rule (marble MOs + masks
from the logic entries): intermediate start, 46 live frames: both equal the MAME PNG
pixel for pixel in a box around the marble; without masks 9 frames differ (up to 156 px).
Not rendered: alpha layer (HUD, opaque columns right of 288).

Remake renderer (`marble_video.cpp` `hide8`): `marble_drawlist::build` converts each entry
of the stored projection +0x1e/+0x20 (same tick as the masks) relative to its owner's origin
(MO top = 240 - y - 8h, camera cancels): marble sprite top left `dx = x - mx`, `dy = (sy - y)
+ 16 - 8 * height`; death frame (sx, 240 - sy) `dx = x - sx`, `dy = sy - y - 8 * height`.
Masks live in a pool of 16 (`VideoState::masks`), each owner holds a range (marble `mask`,
`nmask`; `Obj::mask`, `nmask`); rule A per owner: a mask hides only its owner's pixels (the
arcade merge also hides earlier MOs of other objects, not seen in the traces). Per owner line
and 8 px column an 8 bit hide mask comes from the mask tile rows (bit reverse for hflip,
height h rows = codes c .. c + h - 1, top first); a set bit skips the pixel. Ranges are cut at
the pool end, a full pool drops further masks. +0x1c == 0 skips the player. Entries without
bit 14 (sparkle stars) are drawn as stars. VideoState 10 -> 48 -> 92 -> 304 B (Phase 4b-0).
Playfield patches: the video core writes the block of `lastPatch()` into the map (practice
only) whenever the published value changes to a non-zero record, clipped to the map; a level
load resets the applied value.

## Level object sprites

Visible level objects are motion objects built by the MO list 0x26f3e (Marble Love
late-game-logic-26f3e.ts, mo-block-emit-1a8d2.ts, buffer-fill-1b12a.ts,
sort-adjacent-objects-1a7a8.ts, slot-insert-sorted-18e6c.ts, helper-18f46.ts). Ported in
`tools/marble_mo.py`.

Draw list. Op 0 of a slot script (0x12896) with mode != 1 calls 0x18e6c(tag, slot index):
a 14 byte entry (type = tag, index, box words x, y, z min, x, y, z max) in the first free of
31 entries at 0x4001dc (pointer table 0x1f0e2), its byte inserted into the list 0x4003bc
(0xff end) before the first entry e with 0x1a80a(e, new) != 0. Slot removal (0x12f44, mode
!= 1) calls 0x18f46 (entry freed, list closed up). Players (type 1, 0x1eff6) and enemies
(types 2, 4, 7-9, 14, 15, 0x29, 0x2a) share the list. Mode 1 slots (patches, walls, tubes,
surfaces) are never in it. Each 0x26f3e: every box refreshed by 0x1b12a, then 0x1a7a8 three
times (stride 1, 2, 3, one pass each, swap when 0x1a80a(a[i], a[i + s]) != 0), then MOs
emitted in list order (link order, later on top). 0x1a80a(a, b) = 0 (keep) when x + y + z max
of a <= x + y + z min of b, 1 (swap) when max of b <= min of a, else by y, x, z overlaps:
ascending x + y + z, back to front. The order
persists across ticks (partial bubble sort), so a port keeps the list, not a fresh sort.

Boxes (0x1b12a, slot x/y/z = high words of +0x0c/+0x10/+0x14, frame header h = rl(+0x3e)):
tag 3: (x + h[4], y + h[5], z - 16) size (h[6], h[7], 16); tag 5: (x + h[4], y + h[5], z)
size (h[6], h[7], 0x18); tag 6: (x, y + h[4], z) size (0x20, h[6], 0x28); player: (x - 3,
y - 3, z + 1) size 6. h[] signed bytes. A new entry is boxed at op 0 time (+0x3e =
0x20c14, so h = 0xffffffff and h[4..7] read ROM 0x000003-0x000006).

Drawn slot tags (0x26f3e type = tag; per race from the spawn data, `marble_mo.py data`):

| race | tag (count) | position | frames, MO set |
| --- | --- | --- | --- |
| practice | 5 goal flags (2) | (576, 512/544), z -36 | list 0x212fe, 4 frames, set 3 |
| beginner | 3 lift (1, mode 2), 5 flags (2) | lift cell (61, 56), z -120 | lists 0x21156/0x21192 (7 records, 4 frames) set 6; 3 global parts 0x211fe/0x2121e, 0x2126e, 0x212b2 set 7 |
| intermediate | 6 wave (zone 9/10 spawn, 2 seen at once), 5 flags (2) | wave start cell (81, 77) | lists 0x2222e, 0x22266, 0x2227a (35 frames) set 7 (frame header >= 0x220ee) or 5 |
| aerial | 5 flags (2); 0x0a-0x0d out of scope | | |
| silly | 5 flags (2) | (152/184, 144) | |
| ultimate | 5 flags (5) | (704-736, 704-784), z -32 | |

Types 10-13 (aerial catapult, gates, bounce box) are drawn too (not checked). Tags >= 0x10
are skipped by 0x26f3e.

Per slot (screen position sx = +0x4e high word, sy = low word, as 0x1b9cc; MO frame header
h = rl(rl(+0x42))): d5 = sx + 0x18 (tag 5: 0x17), d4 = sy + 0x10; culled when d4 <= -0x40 or
d4 >= 0x160 (tag 3; ROM 0x27a44, Marble Love lacks the lower bound), 0x100 (tag 5), 0x140
(tag 6). Tag 3 emits the frames of rl(0x40044a), rl(0x40044e), rl(0x400452) with code | 0x3800,
then its own frame | 0x3000. Tag 6 (ROM 0x27f22, Marble Love inverts the test and compares the
record pointer) emits its frame | 0x3800 when h >= 0x220ee else | 0x2800; with list 0x2227a it
adds 4 priority MOs (x word | 0x8000): word w = 0x20f64[i], i = (+0x42 - +0x46) >> 2 (0xffff:
none), code w & 0x7ff, height (w >> 11) & 7 (no hflip, ROM 0x27fa2), x = d5 + 8 + 8k, y = d4 -
0x40 + s8 0x20f92[i] + 4k (k = 0-3).

MO frame (0x1a8d2, header 12 bytes): s8 x bias, s8 y bias, 2 bytes, 4 box bytes (above), long
body (bit 0 = hflip: hflip bit in every MO, columns step -8 px). Body 0xff: short form, 1 byte
skipped, count, s8 dx, then count x (height - 1, s8 dy, code word); else long form: count, s8 dx,
height - 1, s8 dy, count code words. Column k: MO x word = (d5 + xb + dx + 8k) << 5, y word =
(d4 + yb + dy) << 5 | hflip | height - 1, code | set bits. One MO per column, tiles code ..
code + height - 1 top down.

Remake coordinates (MARBLE.md "Marble sprite"): column k at race x = sx + xb + dx + 8k, race
row top + 240 - (sy + yb + dy) - 8 * height, top = camera - descriptor +0x10 (same form as the
marble). MO code: tile = 0x800 + (code & 0x7ff), palette set = bits
11-13 (PROM 136033.118/119 entries 0x00-0x3f all bank 2, 4 bpp), colour 0x100 + set * 16 + pen.

Masks and order:
- Slots get no 0x264aa masks (only objects of 0x251de run 0x1281c); a slot entity emits frame
  MOs only (all checked lists).
- MAME merge: a priority MO pixel with pen 1 shows the playfield, pens 2/3 the shade colour
  0x300 + ((playfield pixel & 15) << 4) + pen, both over every MO earlier in link order. So the
  marble's masks would also erase slot MOs drawn before the marble. Never seen: 0 mask pixels
  over an earlier slot MO in 9 traces (5523 ticks with slot MOs, 1553 with a slot behind the
  marble); the wave strips never covered an earlier MO (190 ticks).
- Shade colours set by the race palette: 0x343, 0x3f2, 0x3f3 (intermediate 0x1ba9, 0x1888,
  0x1ba9), every other 0x3xx entry 0: the wave strip (pens 0-3) darkens the playfield (pen 1:
  unchanged, pen 2: black unless playfield pixel 15, pen 3: black unless pixel 4 or 15).

Palette: MO sets 3, 5, 6, 7 come from the race palette set (0x1fc10, pens 0-7, unchanged in
every shot of 4 runs). Tag 3 also queues a palette command per drawn frame (0x13334: byte
0x1df18[(+0x3e - +0x46) >> 3, +7 for list 0x21192] = command 0-3, 0x26b66 queue, drained by
the vblank IRQ 0x26b88): 4 words to 0x161-0x164 (set 6 pens 1-4) from descriptors 0x20ae4
(0x3eee/0x4fff/0x8fff/0xafff, 0x1eee/0x3eee/0x7eee/0x8fff, 0x1bbb/0x1eee/0x4fff/0x7eee,
0x1888/0x1bbb/0x3eee/0x4fff): the lift's shading follows its height. Palette RAM in the shots
shows the drawn frame's command in 276 of 301 shots (b2), the previous tick's in 24 (all at a
frame change).

Timing: 0x26f3e runs at the end of the main loop after the slot update 0x12fd0, from the slot
state (+0x4e, +0x42, 0x40044a/e/52) and the marble projection (+0x1e/+0x20) of the same tick;
the bank is shown from the next vblank (0x40039a -> 0x4003ae). A snapshot at the next tick's
0x121b8 entry reproduces it (`check`). The terrain of a mode 2 frame (0x1344c) only applies at
the start of the next tick, one tick after the drawn frame. The remake's publish() after
objects::update sees the same state.

Cost (traces, `marble_mo.py`): most slot MOs per frame 4 (practice), 13 (beginner: lift 8
columns + parts 2 + 1 + 2), 35 (intermediate: 2 waves of up to 10 + 4 strip + flags), 10
(ultimate); most on one line 4, 10, 14, 6; most touching one 8 line strip 4, 10, 15, 6. Tiles
(`marble_mo.py data`): practice/silly/ultimate 21 (flags, pens 0-4), beginner 168 (lift 147,
pens 0-7), intermediate 531 (wave 390 + strip 120, strip pens 0-3), 678 distinct over all races
= 21696 B at 4 bpp; frame data (lists, headers, bodies) 112 / 720 / 1821 B.

Validation (`tools/marble_mo.py`, traces in $TMPDIR, `mame_trace.lua` with `MARBLE_PLACE`):
- `check` (model MO list from snapshot work RAM == the MO RAM bank 0x4003b0, words and links):
  b1 794 snapshots with slot MOs, b2 644, p1 292, u1 335, i1 428, cam0 785, cam1 827, cam2 679,
  cam5 739; every slot entity equal except 30 after a lag frame (logic tick of 3+ frames,
  ultimate, MO y 2 px off).
- `sort` (previous list + 0x18e6c inserts + 0x18f46 removals + box refresh + 3 passes == list):
  all 9 traces equal, 8201 pairs, 287 with a new order, 29 inserts, 25 removals (teleport
  frames skipped).
- `render` (MAME PNG, MO RAM render, remake rule with the live palette RAM; box +-8 px around
  each slot object): b1 212, b2 151, p1 128, u1 380, i1 437 boxes; 0 differing MO pixels. 4
  boxes differ in playfield pixels only (scroll redraw timing, same in the MO RAM render). 564
  shots compared; 88 more matched no snapshot (shot taken mid tick) and are skipped. Marble over the lift
  194 ticks, lift over the marble 46, wave over/under the marble 297/30 ticks: order matches.
- Teleport runs: `MARBLE_PLACE=800:500:436:-120,1600:500:490:-120` (beginner, lift),
  `800:540:528:-44` (practice), `800:720:744:-32` (ultimate), `800:652:652:-236:9` (intermediate,
  zone 9 spawns the wave); heights from `marble_sim.py` terrain. `MARBLE_SHOTS` also writes
  shots_pal.bin (palette RAM per shot).

```sh
python3 romconv/internal/marble/tools/marble_mo.py check TRACE RACE
python3 romconv/internal/marble/tools/marble_mo.py sort TRACE RACE [PLACE_FRAME ...]
python3 romconv/internal/marble/tools/marble_mo.py render DIR/shots.bin DIR/snap/marble \
  DIR/trace.bin RACE OUTDIR [N ...]
python3 romconv/internal/marble/tools/marble_mo.py data
```

## Camera

- 0x13ee6 (main loop via 0x10fce), every 8th frame or every frame while a scroll is
  running (0x400008). d3 = lowest sy of the active players (silly race: inverted).
- Thresholds (0x1f1ca..0x1f1d6): sy < 0x48 scrolls forward (camera +), sy > 0xa8 back;
  speed per call 4 when beyond the threshold by more than 0x30, 2 by more than 0x10, else
  the previous speed; a running scroll continues until sy is back inside 0x58-0x98.
  Camera clamped to descriptor +0x10 .. +0x10 + +0x12; playfield y scroll 0x400000 moves
  by the same step (mod 0x200). Traces: camera steps +1/+2 per tick, only while
  sy <= 0x58.
- Details (0x1411c-0x144e2): runs before the object update (0x10fce, 30 Hz), when the tick
  counter 0x400010 & 7 == 0 (every 8th tick) or a scroll runs (0x400008). Constants 0x1f1ca:
  speed bytes 1, 2, 4; words low 0x48, high 0xa8, near 0x10, far 0x30. Only players in slot
  1, state 0/1/3/4/5 and not falling (airborne with vz <= 0) give sy; none (sentinel 0x7000,
  silly 0x9000) while scrolling: a ghost sy 0x40000c continues at the current speed (low or
  high, -/+ near or far) and moves with the camera. Forward when sy < low or a forward
  scroll runs: speed 4 below low - far, 2 below low - near, 1 above low (else kept); back
  mirrored around high. Direction 0x400004 (1 / 0xff), speed 0x40000a. A scroll stops at the
  race range or once sy > low + 0x10 (forward) / < high - 0x10 (back).
  Initial camera: +0x10, silly +0x10 + +0x12 (0x16f4a); scroll state 0 (0x10540).
- Remake port `marble_camera::tick` (source/src/machines/marble/marble_camera.cpp): equal
  to the traces in all 9640 ticks of 10 traces (642 camera moves, races 0-5), camera
  long, playfield scroll, direction, speed, scroll flag and ghost sy. Not ported: row
  decode (the remake renders from the whole level) and 0x144e4 (objects entering the
  screen, Phase 4).
- Remake view (240 x 288, the arcade screen is 240 lines): view top = camera - +0x10 -
  24, clamped to 0 .. range - 48, so the arcade screen sits in the middle. X (proposal,
  remake only): pan 0..48, keep the marble centre sx between view x 64 and 176, 1 px per
  60 Hz frame, 2 px when 16 px beyond, 4 px when 48 px beyond.

## Remake data

`./convert.sh marble` (romconv/internal/marble/marble_rom_convert.py) appends the physics
data to `source/src/machines/marble/marble_rom.h` (generated, untracked). Terrain comes from
`Terrain` in tools/marble_sim.py, so the bytes equal the MAME video RAM image (`marble_sim.py
terrain`). Flash: 45937 B (terrain 38870, program ROM blocks 4237, marble sprites 2830).

Terrain, `marble_terrain[race]` (`MarbleTerrainInfo`):

- `index`, `pairs`: 10 bit vertex index, 54 bytes per diagonal pair (pair p = d >> 1),
  fields MSB first, field f at bit 10 * f, f = column (0-21) + 22 on odd diagonals.
  Field 43 is not stored: index 0.
- `codes`, `ncodes`: code table (descriptor +0x26, `Terrain.codes`), races 1-5 share one.
- `records`, `nrecords`: descriptor +0x00 records from offset 0 to the 0xffffffff end;
  record code c (0x0001-0x07ff), quadrant q: `records[c + q]`.
- `bases`, `maxd`: base height word per diagonal (`Terrain.bases`), diagonal count.
- `desc`: 68010 address of the level descriptor (start cells +0x14, camera +0x10/+0x12).

`Terrain.index(d, f)` = `(be16(index + 54 * (d >> 1) + 10 * f / 8) >> (6 - 10 * f % 8)) &
0x3ff` for f < 43, 0 for f = 43 (a 16 bit read never leaves the pair). Code -> heights:
`code_heights` (0x1cb64); dynamic codes read work RAM 0x40076e.

| race | diagonals | index | codes | records | bases |
| --- | --- | --- | --- | --- | --- |
| practice | 160 | 4320 | 160 (320 B) | 80 | 320 |
| beginner | 216 | 5832 | 982 shared (1964 B) | 156 | 432 |
| intermediate | 192 | 5184 | shared | 376 | 384 |
| aerial | 216 | 5832 | shared | 108 | 432 |
| silly | 238 | 6426 | shared | 192 | 476 |
| ultimate | 200 | 5400 | shared | 44 | 400 |

Program ROM, `marble_prog_blocks[MARBLE_PROG_BLOCKS]` (`MarbleProgBlock {addr, len, data}`,
sorted): every byte below 0x30000 that `marble_sim.py` reads in `tick()` and `start_259b4`
(recorded over all fixture ticks and the start of all 6 races) plus the whole tables:
0x24b3a vertex steps, 0x1ed62 drop, 0x23d40 edge push, 0x2398c teeter, 0x1eef8 friction,
0x1eb16 zone line pointers + lists 0x239b8-0x23c03, 0x1eb2e/0x1eb34 goal velocity,
0x1ef1a checkpoint pointers + lists 0x1f1d8-0x1fbcf (respawn), 0x1f1ca camera, 0x2be00
and the 6 descriptors (0x2e bytes), 0x1d370 intermediate dynamic codes, first dynamic code
frames 0x1699c, 0x1b908, 0x24e4e. The converter
fails when the sim reads ROM outside these, and replays starts and fixture on the emitted
blocks only. Slapstic reads (0x80000+) are terrain records, served by `records`. A port's
ROM accessor should fail on an address outside the blocks.

Race start (`start_259b4`, validated for all 6 races: `marble_sim.py start TRACE RACE`
equals the run from the first race snapshot): cell from descriptor `+0x14 + 2 * player`,
`x/y = cell * 8 + 4`, z from 0x1bab2 + 0x1cc62, physics fields 0, +0x18 = 1, aerial
+0x58 = 0x17. Needs the dynamic terrain codes first (`dyn_init`): intermediate copies 66 words
from 0x1d370 (0x11ac2, level init 0x105be). Practice, beginner and ultimate get theirs from
level object animations (section Level objects; `objects_init` + the slot update give the
exact arcade values); `dyn_init` holds them at the first animation frame, read
from ROM like the copy loop 0x13628 (word slot, word count, words): practice 0x1699c (9
words, all 0xf040), beginner 0x1b908 (12), ultimate 0x24e4e (112). MAME write tap on
0x40076e (all writes from PC 0x13638): practice writes that frame at level init, beginner
and ultimate are 0 (no floor) at the first controllable tick and get the frame when the
objects spawn (camera driven forward: frames 817 and 905), then animate (beginner 16-23,
ultimate 8-13 values per slot up to frame 1600). The converter fails if a dynamic slot the
terrain uses is not set or points outside the records.

Marble sprite: `marble_mo_tiles[k][64]` (pens 0-7, 0 transparent) for MO codes 0x03-0x2d,
tile t of an MO of code c and height h at `marble_mo_code_first[c] + t` (t < h, 0xff = not
emitted). Lookup PROM entry 256 + (code >> 8): 136033.118 = 0xd8 (bank 2, tile offset 8:
graphics tile 0x800 | code), 136033.119 = 0x0f (4 bpp, palette set 0). `marble_mo_palette`
= palette RAM 0x100-0x10f (RGB565 byte-swapped, same in every race; pens 8-15 unused).
Player MOs in the traces: 0x05 (h 2, left half), 0x03 (h 2, right half, x + 8), 0x07 (h 1,
MO y + 5, top 3 screen lines lower), dots 0x0d-0x21 (h 1, hflip used); 0x22-0x2c (h 2) and higher codes appear
around deaths.

Host test fixture `source/test/test_marble_physics/marble_trace.h` (committed, derived
numbers only), `tools/make_fixture.py`:

- `marble_trace_segments[MARBLE_TRACE_SEGMENTS]`: race, MAME frame, initial work RAM
  blocks `{addr, len, bytes}` (everything else 0: object 0x400018 + 0xe2, race words
  0x400390-0x400395, scratch 0x400660-0x4006a7), ticks `{c6, c7, x, y, z, state}`.
- Per tick: `tick(c6, c7)` as in marble_sim.py (physics of the current tick, then the new
  deltas, 0x253bc and input), then x, y, z (16.16) and state +0x1a must match. The C++
  `tick` starts at the handler (deltas, 0x253bc, input, physics): the host test finishes
  the snapshot's tick first (physics internals), then runs tick k with the deltas of k - 1.
- Segments (no level object slots, dynamic codes and enemy scratch from the initial RAM
  only; each tick also matched the trace in velocity, +0x1b, +0x36, +0x37, +0x56-+0x58):
  practice frame 2056 of trace p4 (360 ticks, rolling, leftward, negative velocities),
  practice frame 1420 of p3 (211 ticks, side bounce, wall, teetering, 31 airborne ticks,
  next tick lost), beginner frame 1187 of b2 (359 ticks, 7 walls, teetering, 26 airborne
  ticks, next tick broken on landing). Practice has no 300 tick segment with a death: its
  random runs fall off within 240 ticks of a respawn. The death ticks are cut: the break
  choice needs the projected sy (no camera word in this fixture) and 0x25bae(4) respawns
  in the death tick; test_marble_deaths covers deaths.
- The beginner segment needs the stale wall outputs (0x400674-0x400683) of its initial
  RAM: zeroing them diverges at tick 345.

```sh
python3 romconv/internal/marble/tools/make_fixture.py search TRACE RACE [MIN]
python3 romconv/internal/marble/tools/make_fixture.py write \
  source/test/test_marble_physics/marble_trace.h 0:p4/trace.bin:2056:360 \
  0:p3/trace.bin:1420:211 1:b2/trace.bin:1187:359
python3 romconv/internal/marble/tools/marble_sim.py fixture \
  source/test/test_marble_physics/marble_trace.h   # replay, no trace needed
```

### Phase 3 data (level objects, occlusion)

Same converter run; flash 16222 B (was 22777 B before the edge lists; budget 25 KB): edge lists
4144 B, program ROM blocks 8014 B (added to the Phase 2 blocks, which stay 4237 B), practice gate
patches 912 B, mask tiles 3152 B. The converter prints these and the saving, fails above 25 KB.

Program ROM blocks (`marble_prog_blocks`, now 35 blocks): collection runs the model with
`objects` and `sprites` on over the race starts, `objects_init` + 200 idle ticks of every race
(practice gate included), the Phase 2 fixture and the object fixture, and records every ROM
byte read (ms.PROG is replaced by a recording `bytearray`, so direct table reads count too).
Added as whole tables, so a port never meets a gap:

- object slots (`objects_data.object_bytes`): spawn list 0x1dec0[race] (6 byte entries + 0xff),
  random group table 0x1ded8 (64 B), every script byte the byte code walk reaches, animation
  lists up to the 0xffffffff end, terrain blocks (word first slot, word count, words), the
  intermediate wave tables 0x24c7a-0x24e27; shared: pointer table 0x1dec0, slot address table
  0x1f016, the three tag 3 global lists (0x211fe, 0x2121e, 0x2126e, 0x212b2 and the long before);
- occlusion (`occlusion_bytes`): 0x1eaba, 0x1ead2, 0x1eaf4 (shape, slope tables),
  0x24176/0x2417e (quadrant masks, vertex offsets), the pointer table 0x1ea2a, the six word
  offsets per column and the explicit occluder lists of all columns and races (175 records of
  10 bytes). The model's `vertex_height_177f8` reads the terrain index like the remake (the
  field map 0x1ed0a is the identity, the pair address words 0x1eb3a only locate the video RAM
  image), so those two tables are not emitted; the bit masks 0x1eaca neither.
- terrain edges (`edge_tables`): not program ROM blocks. The arcade scan (first diagonal >= d
  with a bit in the bitmap of descriptor + 0x2e + 4 * col, ultimate: 4 tables, + 0x90 * frame,
  frame = 0x40045c / 0x90) becomes per race and frame, per column 0-35, the ascending list of
  diagonals < maxd with a bit (`marble_edge_lists`, uint8_t, maxd <= 238, equal lists shared:
  3448 B) and `marble_edge_off_<race>[36 * frames]` (uint16_t list offsets, 648 B; pointer and
  frame count in `MarbleTerrainInfo`). The last entry is always maxd - 1, so for d < maxd a
  linear scan (`Mem::edgeFrom`) ends at an edge; d >= maxd is no edge (remake guard). The
  converter checks the lookup against the bitmap scan for every race, frame, column and d, and
  the model (`marble_sim.EDGES` hook) replays on the lists. Was 9504 B of bitmaps plus the
  pointer tables.

Slapstic reads stay terrain records. The converter replays all runs on the emitted blocks only
(`BlockProg` fails on any other address) and requires the same results as on the full ROM.

Practice gate patches (`marble_patches[MARBLE_PATCHES]`, `MarblePatch {record, col, row, w, h,
words}`): the 12 animation records of the tag 0x11 gate (lists 0x16658, 0x1668c; 6 distinct tile
blocks of 10 x 6 words, bytes w, h, then w x h playfield words). `record` is the animation record
address 0x1344c is given (0x400970 queue = `m.rl(a + 0x3e)`), `col`/`row` the slot's patch
column and race row from the script (14, 3), `words` map words (local tile | colour set << 10 |
hflip << 15) written row-major at (col, row) of the level map. The tiles missing from the
practice tile set were appended in first-use order (practice 760 -> 863 tiles, MAX_TILES 896 kept;
the converter fails above). Check: record 0x16658 equals the decoded level map (closed gate). Tag
0x19 (ultimate rows) is not emitted (Phase 4).

Mask tiles (`marble_mask_tiles[394][8]`, `marble_mask_index(code)`: code - 0x4df or 0xffff): the
tiles of every entry code 0x264aa can emit (explicit occluders except control -2 and the
placeholder code 0, the slope table 0x1ead2, full mask 0x6ce0; height from bits 11-13): MO codes
0x4df-0x668 (not 0x4e0-0x747: no entry reaches beyond 0x668, 0x4df is used by 0x44df). 1 bpp, bit
7 = left pixel, 1 = mask (pen 1; the converter fails on a pen > 1). `marble_mask_index` returns the
index of a tile code or 0xffff. The 18 codes in the range no entry uses are blank tiles (376
real tiles), which draws the same as no tile: the 788 B map is gone.

Object fixture `source/test/test_marble_objects/marble_objects_trace.h` (committed, derived
numbers only), `make_fixture.py owrite`:

- `marble_objects_segments[MARBLE_OBJECTS_SEGMENTS]`: race, MAME frame, initial work RAM blocks
  (player, race words, scratch, all 25 slots 0x400a9c-0x401301, all 128 dynamic codes
  0x40076e-0x40086d, slot globals 0x40045c, 0x40075c, 0x400970 and whatever else the run reads
  first), ticks.
- Tick `{c6, c7, camera, x, y, z, state, vis, mask[30], slot_crc, dyn_crc}`: `tick(c6, c7, camera)`
  of the model with `objects` and `sprites`, then player x, y, z, state, +0x1c (`vis`, 0 = marble
  hidden), the 30 bytes at +0x38 (5 entries code, x, y, big endian), crc32 (zlib) of the 2150
  bytes 0x400a9c-0x401301 and of the 256 bytes 0x40076e-0x40086d (big-endian RAM bytes).
  `camera` is the camera long 0x40097c after the tick, an input like the trackball deltas
  (marble_camera computes it from the player, the model does not). Nothing else comes from
  the trace after the first snapshot. Each tick also equals the trace in the slot fields
  (`slot_diff`: active slots, dynamic codes), the object bytes of Phase 2, +0x1c-+0x29 and the
  masks; a segment ends before the first difference (death, frame gap, enemy within 24 px).
- Segments (events counted by the model): practice mt/p3 frame 730, 340 ticks: 3 slot wall
  contacts (ticks 232, 319, 333), the gate opening (10 dynamic code changes, ticks 153-266), 71
  ticks with masks, 12 with hflip; beginner lo/cam1 frame 2399, 340 ticks: the moving part (tag
  3) active 111 ticks, 23 dynamic code changes, 206 ticks with masks, all with the marble hidden
  (camera driven away from it), no hflip, spawns and removals; intermediate lo/i2 frame 660, 251 ticks (to the death):
  39 ticks with masks, 14 hflip, 8 hidden; intermediate mt/q2 frame 1942, 300 ticks: 92 ticks
  with masks, 48 hflip, 10 round post contacts. lo/pw and lo/pw2 do not open the gate within
  their ticks (no dynamic code change), so the practice segment is a Phase 2 trace.
  Over the four segments (1231 ticks): 408 with masks, 313 of them with more than one entry, 74
  with hflip, 214 with the marble hidden.

```sh
python3 romconv/internal/marble/tools/make_fixture.py osearch TRACE RACE [MIN]
python3 romconv/internal/marble/tools/make_fixture.py owrite \
  source/test/test_marble_objects/marble_objects_trace.h 0:mt/p3/trace.bin:730:340 \
  1:lo/cam1/trace.bin:2399:340 2:lo/i2/trace.bin:660:251 2:mt/q2/trace.bin:1942:300
python3 romconv/internal/marble/tools/marble_sim.py objfixture \
  source/test/test_marble_objects/marble_objects_trace.h   # replay, no trace needed
```

### Phase 4a data (level object sprites)

Same converter run; flash 9327 B (converter prints it: prog blocks 874, tiles 5131, frames 328,
columns 2370, header ids 118, palettes 452, set table 54). `emit_sprites`; model and ids in
`tools/marble_mo.py` (`Sprites`, `DrawList`, `publish`).

- Sprite sets (`marble_sprites[]`, `MarbleSpriteSet {frames, cols, addr, nhdr, nframes,
  ntiles}`, `marble_sprite_race[6]` race -> set): races with equal frames and tiles share a set
  (practice/silly/ultimate 21 tiles, beginner 168, intermediate/aerial 531; aerial gets the wave
  data too because the walker reaches the wave script there). Tiles
  `marble_sprite_tiles[set]` (`FlashAsset<unsigned char[32], COMPRESSED>`, 4 bpp, left pixel in
  the high nibble, pens 0-7, sorted MO code order): raw / LZMA 672 / 271 B, 5376 / 1203 B,
  16992 / 3609 B; `MARBLE_SPRITE_MAX_TILES` = 750 (24000 B Arena buffer: death set 219 +
  largest set 531, Phase 4b-0).
  Identical tiles are not merged: columns read consecutive tiles, merging would need an index
  per tile, and LZMA folds the duplicates (merged 412 tiles / 3750 B vs 3609 B unmerged for
  intermediate, 146 vs 168 tiles and 1370 vs 1203 B for beginner; hflip merging saves 1 more
  tile in 2 sets). Column `hflip` stays the frame's.
- Frame ids per race: header frames in ascending header address order (4 / 20 / 35), then the 23
  wave strip entries (list 0x2227a; `strip = nhdr`; `ncols` 0: no entry): 4, 20, 58 ids.
  `addr[k] = (header - 0x20000) >> 1` ascending, id = index (binary search of the header
  rl(rl(+0x42)) of a slot). `MarbleSpriteFrame {ncols, first}`, `MarbleSpriteCol {x, y, h | hflip
  << 7, tile}`: x = xb + dx + 8 k (-8 k flipped), y = -(MO y offset + 8 h) with MO y offset
  yb + dy (strip: 0x20f92 s8 - 0x40 + 4 k, x = 8 + 8 k, one tile code per entry, height from the
  word), `tile` = buffer index of the first of h consecutive tiles (race tiles start at
  `MARBLE_DEATH_TILES`; `ntiles` = buffer tiles the set reaches). Identical column lists share
  columns (327 columns for 331).
- Palettes: `marble_sprite_pal[race][4][8]` sets 3, 5, 6, 7 pens 0-7 (RGB565 byte swapped),
  `marble_sprite_shade[race][3]` entries 0x343, 0x3f2, 0x3f3 (all other 0x3xx black, checked),
  `marble_lift_pal[4][4]` tag 3 variants (set 6 pens 1-4).
- Program ROM blocks: the draw list code reads, besides the Phase 3 tables, bytes 4-7 of every
  frame header of every mode 0/2 animation list of every race, drawn tag or not (box, 0x1b12a;
  tags 0x0a-0x0d sit in the list too), with the list entries, 0x3-0x6 and 0x20c14 (empty
  animation), the tag 3 palette command bytes 0x1df18 (14), and what the runs read of enemy boxes
  (0x1eff6, 0x1effe, 0x1f00a and others). The sprite fixture and a draw list sweep per race
  (`marble_mo.race_sweep`: race start incl. draw list, camera over the whole race down, up, down
  in 4 line steps, update, sort, publish at margin 24) are replayed on the blocks only like the
  others. Largest published counts of the sweep: practice 2, beginner 4, intermediate 2, aerial
  2, silly 2, ultimate 5 (host test `test_race_sweep`).
- The draw list in work RAM is the arcade one: `DrawList.insert` 0x18e6c at op 0, `remove` 0x18f46
  (`Mem.drawlist` hooks in marble_sim.py), `refresh_sort` 0x26f3e head (boxes, three passes).
  Entry k is at 0x4001dc + 14 k (the table 0x1f0e2 is not needed).
- Publish (`marble_mo.publish`, draw list order, back to front): per slot the arcade cull
  (tag 5 `sy + 0x10` outside -0x40..0x100, tag 3 -0x40..0x160, tag 6 -0x40..0x140), widened by
  `CULL_MARGIN` (0 in the fixture, remake 24). Objects `(frame id, set, x, y)`: x = sx (tag 5:
  sx - 1), y = camera word 0x40097e (as the slot update saw it) - descriptor +0x10 + 240 - sy;
  set 3 flags, tag 3 parts 7 (three globals, then the own frame set 6), tag 6 5 or 7
  (header >= 0x220ee) and 8 for a strip entry (priority shade MOs). At most 8 objects (fixture
  max 6). `marble_at` = objects before the marble, `variant` = 0x1df18[(+0x42 - +0x46) >> 3, +7
  for list 0x21192] of the drawn lift frame, 0xff otherwise. Expanded to MO columns the objects
  equal `mo_list_26f3e` in every fixture tick (checked when writing).
- Remake renderer (`marble_video.cpp`): `Store::load` decodes the race's sprite set into one
  Arena buffer of 750 tiles (24000 B, allocated once in `init`) behind the death set (Phase
  4b-0, decoded on the first load); skipped when the new race uses the same set. The buffer
  no longer fits behind map and playfield tiles (67784 B) in Arena block A: it takes block B
  (91784 B of 182000 in all). Per strip: playfield 8
  lines, objects `0 .. marbleAt - 1`, the marble (hide masks, rule A), objects `marbleAt .. n -
  1`. Per object a frame column is culled once per strip (rows, view columns, tile index
  against the set), then drawn line by line: pen 0 transparent, hflip per column, clip at the
  240 px edges. Set 6 pens 1-4 come from `marble_lift_pal[lift]` when `lift < 4`. Set 8 (wave
  strip): pen 1 skipped, pens 2/3 write `marble_sprite_shade[race]` when the playfield pixel
  under it (0..31) & 15 is 4 (pen 3) or 15 (pen 2, 3), else black. Pen 1 does not erase earlier
  MOs here (the MAME merge does; never seen in the traces). Cost: the culling is
  n x columns per strip (at most about 8 x 10), the pixel loop only runs for overlapping
  columns, at most 15 per strip.
- Culls at margin 24 (remake view, 24 lines more above and below), most objects in the fixture
  segments: beginner 4, intermediate 6, ultimate 5, practice 2; 8 hold
  (`test_margin_fits_objs`). `OBJS` 24 since Phase 4b-0 (player death objects, enemies).

Sprite fixture `source/test/test_marble_sprites/marble_sprites_trace.h` (committed, derived numbers
only), `make_fixture.py swrite`: segments of the research runs between `MARBLE_PLACE`
teleports, initial work RAM blocks (player, slots, dynamic codes, draw list entries 0x4001dc-
0x4003e3, tag 3 globals, first reads), per tick `{c6, c7, camera, n, marble_at, variant,
first}` and `MarbleSprObj {frame, set, x, y}[]`. The model (sim with the draw list hooks, sort,
publish) reproduces every published list from these alone (`marble_mo.py spritefixture`) and
equals the MAME draw list bytes in each tick. A segment ends before the first tick where the
list differs: enemies are not ported, their entries move in MAME. A 3 frame logic tick (lag) is
accepted for sprites. Host test `test_marble_sprites` checks the emitted tables against the
fixture.

| segment | ticks | events |
| --- | --- | --- |
| beginner b1 853 | 35 | lift inserted (0x18e6c), lift drawn over the marble 6 |
| beginner b1 933 | 200 | lift over the marble all ticks, 31 dynamic code changes |
| beginner b2 933 | 198 | lift over the marble 109, marble over the lift 89, 1 reorder |
| beginner b2 1601 | 120 | marble over the lift all ticks, 4 mask ticks |
| intermediate i1 1212 | 260 | 2 waves + strips (114 ticks with a strip), flags, 6 inserts, 6 removals, 6 reorders, max 6 objects |
| ultimate u1 1094 | 150 | 5 flags, max 5 objects |
| practice p1 800 | 100 | flags 43 ticks, 1 insert |

```sh
python3 romconv/internal/marble/tools/make_fixture.py ssearch TRACE RACE [MIN]
python3 romconv/internal/marble/tools/make_fixture.py swrite \
  source/test/test_marble_sprites/marble_sprites_trace.h 1:mo/b1/trace.bin:853:35 \
  1:mo/b1/trace.bin:933:200 1:mo/b2/trace.bin:933:198 1:mo/b2/trace.bin:1601:120 \
  2:mo/i1/trace.bin:1212:260 5:mo/u1/trace.bin:1094:150 0:mo/p1/trace.bin:800:100
python3 romconv/internal/marble/tools/marble_mo.py spritefixture \
  source/test/test_marble_sprites/marble_sprites_trace.h   # replay, no trace needed
```

### Phase 4b-0 data (deaths)

Same converter run; flash 3772 B (converter prints it: prog blocks 900, tiles 1864, frames
176, columns 696, header ids 88, palette 32, set 16). `emit_death`; model in
`tools/marble_mo.py` (`DeathSprites`).

- Death set (`marble_death_set`, `MarbleSpriteSet` as the race sets, ids = ascending header
  order, `nframes = nhdr`, no strip), the same in every race: the frames of the player
  animation lists rl(+0x5a) 0x20faa, 0x20fb6, 0x20fd2 (dizzy, eaten), 0x20fde (shatter),
  0x21062 (dissolve) and the shatter overlay rl(+0x62) list 0x215f6 entries 0-43 (+0x62
  steps once per tick from shatter frame 10 to the list end: (32 - 9 - 1) x 2 ticks; entries
  44+ are never drawn). 44 frame headers, 116 columns. `deathFrame(hdr)` (marble_mem): id
  by binary search, aborts on a miss like `spriteFrame`.
- Tiles `marble_death_tiles_blob` (LZMA, format of the race sets): 219 tiles (every code of
  the 44 frames plus the sparkle stars), 7008 / 1848 B. `MARBLE_DEATH_TILES` = 219. The
  sprite tile buffer holds them first (decoded once, first `Store::load`), the race set
  after them: race set columns store `tile + MARBLE_DEATH_TILES`, death columns index the
  buffer directly.
- Sparkle stars (state 4, 0x13ade): single MOs of code 0x10b + k (k < 8, height 1, set 0),
  not frames. Sorted codes make them 8 consecutive tiles: buffer tile `MARBLE_STAR_TILE`
  (187) + k, `MARBLE_STAR_CODE`, `MARBLE_STARS`. Three constants, smaller than a code table
  (`marble_mo_code_first`) or 8 one-column frames.
- Palette `marble_death_pal[2][8]`: set 0 (player 1 frames, +0x19 = 0, and stars), set 2
  (overlay, code | 0x1000), pens 0-7 RGB565 byte swapped; equal in every race (checked). The
  converter checks that code | set << 11 looks up the same tile for both sets.
- Program ROM blocks: collection also replays the death fixture (main tick with deaths,
  objects, sprites; the check values read the full ROM since the player MO model reads frame
  bytes the remake replaces by the death set) and requires every drawn frame header, read
  through the blocks, to be in the death set. Whole tables added: the 5 animation lists with
  their end, overlay entries 0-43, sine 0x1eda2 (0x66 words), 1/sqrt 0x1ee6e (0x35 words),
  dot codes 0x24ade (39 words) and y bias 0x24b2c (13), sparkle offsets 0x1ef32, dot rest
  pose 0x1d3f4 (16), time out limits 0x24a94 and zone lists 0x24aae.

- Remake renderer: `marble_drawlist::build` publishes the player at its list position as
  `marble_mo.type1` draws it: states 1/2/5/9 (frame rl(+0x5a)), 4 (stars) and 0xa/0xb hide the
  marble. Objects: frame (`Obj::set` `SET_DEATH`, palette set 0) at the player origin (sx,
  camera top + 240 - sy) with the +0x38 masks; stars (`SET_DEATH | SET_STAR`, frame = k, one
  tile at the star's top left) of +0xa4 and of +0x38 entries without bit 14; overlay
  rl(+0x62) while +0x67 (`SET_SHARDS`, set 2). `drawObj` takes the death set and
  `marble_death_pal` for `SET_DEATH`. Host tests: `test_player_objects` (every death fixture
  tick: columns, stars and masks as MO words, frame headers, marble shown iff MAME draws 0x05;
  one tile per code in code order), `test_object_masks`, `test_mask_pool` (17 masks),
  `test_death_edges` (reference composite at every view edge, guarded tile buffer).

Death fixture `source/test/test_marble_deaths/marble_deaths_trace.h` (committed, derived
numbers only), `make_fixture.py dwrite`: segments of the tick start traces pd1/bd2 (section
Player states, deaths, RNG), initial work RAM blocks (player, race words, 0x400444, slot
globals, scratch, 25 slots, dynamic codes, the +0x18 bytes of steelies and slinkies 0x17cb8
reads, vertex cache), per tick `MarbleDeathTick {c6, c7, camera, frames, x, y, z, state,
vis, sx, sy, anim, frame_hdr, shard_hdr, mask[30], rng, n, first}` and the player MO words
`MarbleDeathMo {w0, code, w2}[]`. Inputs: trackball deltas of the tick, camera long after
the camera step, 0x400010 at the tick start. `main_tick` reproduces every value from these
alone (`dfixture`) and equals MAME in each tick.

| segment | ticks | events |
| --- | --- | --- |
| practice pd1 802 | 60 | fall 22 px, dizzy 44 (frames 0x20faa, 0x20fb6, 0x20fd2) |
| practice pd1 1002 | 168 | fall 70 px, shatter 64 (11 frames, overlay 17), sparkle 24, respawn (10, 22), 60 rolling |
| beginner bd2 1971 | 148 | off the edge, fell, sparkle 48, respawn (78, 95), 60 rolling, 36 mask ticks |
| beginner bd2 2501 | 80 | fall 24 px, dizzy 48 |

```sh
python3 romconv/internal/marble/tools/make_fixture.py dwrite \
  source/test/test_marble_deaths/marble_deaths_trace.h 0:pd1/trace.bin:802:60 \
  0:pd1/trace.bin:1002:168 1:bd2/trace.bin:1971:148 1:bd2/trace.bin:2501:80
python3 romconv/internal/marble/tools/marble_sim.py dfixture \
  source/test/test_marble_deaths/marble_deaths_trace.h   # replay, no trace needed
```

### Phase 4b-1 data (steelies)

Same converter run; flash 849 B (converter prints it: prog blocks 646, sprite sets 203).

- Steelie frame 0x21f36 (2 columns, 4 tiles, set 5) is in every race sprite set, not only
  beginner/aerial/ultimate: races keep sharing sets (3 sets: practice/silly/ultimate,
  beginner, intermediate/aerial). Only in aerial it would split aerial from intermediate
  (+6.4 KB). Largest set 535 tiles, `SPRITE_TILES` = 219 + 535 = 754 (24128 B Arena).
- A shattering steelie draws the death set's shatter frames with set 5 (code | 0x2800): the
  converter checks that set 5 looks up the same tiles as sets 0/2; colours come from the
  race's set 5 palette (`marble_sprite_pal`), no new palette. Overlay +0x62: set 2 as the
  player.
- Program ROM blocks: collection replays the steelie fixture and the steelie race sweep
  (`marble_enemy.sweep`, all races); whole tables: spawn table 0x22706 (pointers, entries,
  0xff end), every entry's node list to its 0xff 0xff end, 0x1eff6, 0x1effe, pair list
  0x24ac6, push table 0x24ad6 (8 bytes), score entry k = 3 of 0x23cd4/0x23cf6. The sweep's
  0x1b12a boxes of slinky/acid entries in the fixture's start RAM read their frame headers
  (blocks only, the remake draws none).
- The sprite fixture was regenerated (`swrite`, same specs): only frame ids shifted.

## Enemies

Research: docs/superpowers/specs/2026-10-11-marble-enemies-research.md (steelie model) and
2026-10-11-marble-enemies-research-b.md (interaction, RNG, plan basis). Model:
`tools/marble_enemy.py` (steelie update and 0x1bc88, Marble Love ports by address).

| array | count x size | draw type, ptr table | races (max live) |
| --- | --- | --- | --- |
| 0x4009a4 steelie (black marble) | 2 x 0x7c | 2, 0x1effe | beginner 2, aerial 1, ultimate 1 |
| 0x401302 slinky (green worm) | 4 x 0x60 | 4, 0x1f006 | beginner 3, intermediate 2, ultimate 4 |
| 0x401482 acid (green puddle) | 7 x 0x42 | 0x0e, 0x1f07a | intermediate 7, ultimate 4 |
| 0x401650 pistons (rising posts) | 36 x 0x10 | 0x29 | aerial (16 seen) |
| 0x401890 minis (small steelie/slinky/acid) | 9 x 0x28 | 7, 8, 9, 0x1f096 | silly (bonus, no death) |
| 0x4019f8 birds | 10 x 0x38 | 0x0f, 0x1f0ba | silly |

Spawning: 0x144e4 calls 0x15a12 (steelies), 0x14c46 (slinkies), 0x17346 (acid) before the slot
spawner 0x12dfa, band = (camera - descriptor +0x10) >> 4. Tables 0x22706, 0x2257a, 0x23d4a
[race]: 8 byte entries (lo, hi, long data, kind byte -> +0x1b, pad; 0xff end), same spawn/remove
rule as slots, free slot = highest free index, no spawn while a live slot has the same data
pointer. Steelie entry 0x2276e (beginner start) has hi - 16. Acid long 0 = random: kind 5 one
path from 0x23d62/0x23d66, else six from 0x23d6a/0x23d82 (rng(2)). Silly: band enters/leaves
0x1d-0x38: minis spawn (0x18ffa, rng positions) / clear (0x190ee); band in 3-0x1b sets
0x400762, birds spawn every 8 frames (0x19a40, points 0x244f6). Aerial: a player in zone 4/5
(0x13a16 -> 0x186ac) fills the 36 pistons and sets 0x400760.

Tick order (0x10fce): spawns in 0x13ee6 -> players 0x251de (0x121b8 runs 0x1bc88 after 0x29cce,
then player only: 0x14e92 slinky, 0x175c8 acid, 0x1881c pistons, 0x1924e minis, 0x19d94 birds,
0x1365c zones) -> 0x158cc steelies -> 0x1493c slinkies -> 0x17230 acid -> 0x1912c minis ->
0x19baa birds -> 0x1844a pistons -> 0x12fd0 slots. The main loop calls rng(0x100) (0x118a4)
once per tick before 0x26f3e (traces: one call per tick plus spawn/enemy calls); modelled in
`marble_sim.main_tick`. RNG consumers in a race: acid 0x173b0 rng(2), aerial random slot
groups 0x12e54 rng(4), pistons 0x1871e rng(4), launch 0x188ce/0x188f4 rng(2), sounds
0x1858a/0x185ea/0x1864e rng(2), minis 0x19054/0x1906e rng(0x20), 0x1960e rng(5)/rng(2) per
mini, birds 0x19c94/0x19cb6 rng(2), tubes 0x29ec8/0x29ee4/0x2a792/0x2a8c0, palette 0x26d20.
A fixture equal to MAME needs every consumer the race reaches. Bird spawns read 0x400010 & 7
(fixture input). 0x17cb8 (respawn spot check) also scans steelies and slinkies.

Steelie (layout = player object +0x00-+0x5f, then +0x62/+0x67 extra frame, +0x68 speed long,
+0x6c anger word (0xffff at spawn, 0x96 at the first hit, counts down), +0x6e node, +0x72 node
list, +0x76/+0x78 band, +0x7a target index into 0x1eff6). Per tick 0x158f6: anger and stun
timers, then (+0x18 == 2) shatter animation, else 0x253bc, AI 0x182ba, player physics 0x121b8
(no gravity, no zones; slots 0x29cce, masks 0x264aa kind 1 at +0x38). Nodes 6 bytes (cell x, y,
4 signed links into +0x72). States: 0x20 patrol (link 0), 0x21 chase the player (same zone,
octant distance < 0x280, angry or zone 7/9, on ground), 0x22 hunt (angry: link nearest the
player, 0x15c46), 0x23 return to the nearest node (0x15d10), 0x24 stunned after a landing.
Speed +0x68 0xc0000 / 0x80000 / 0x70000 (0x10000 near the target); v += ((k * dir >> 4) - v)
>> 5, then speed cap 0x26196. Model equals MAME byte for byte (5 traces, 3104 steelie ticks,
states 0x20-0x23, 3 collisions).

0x1bc88 (pair list 0x24ac6 = P1, P2, steelies; races 1/3/5 all four, else players):
|dx|, |dy| <= 7, |dz| <= 14, octant < 0x70, partner not in state 2/4/0xb: mover back to the
old position, velocities swapped, each pushed by s8 0x24ad6[|v high word|] << 12 (x4 marble vs
steelie); steelie: anger, +0x56 += 0x14, state 0x23. Mover airborne and not below: partner
breaks (player 0x25bae(2), steelie shatters). Non-player landing 0x25c74: state 0x24, +0x56 +=
fall, shatter above 0x50 or fall > 0x1f; fall > 128 px / embedded: 0x15bd0 (slot freed).

Player deaths by enemies: slinky 0x14e92 -> state 5 (carried, anim 0x20faa/0x20fb6) -> 7
(hidden) -> 4; acid 0x175c8 -> 9 (anim 0x21062) -> 4; bird 0x19d94 -> 0xb (+0x57 0x66, orbit
0x13ade) -> 4; steelie from above -> 2 (anim 0x20fde). State 4 entry (0x25bae) places the
respawn (0x2591a).

Drawing (0x26f3e, frames 0x1a8d2): type 2 at +0x1e, visible by +0x1c, frame 0x21f36 set 5,
masks +0x38; type 4 at +0x28, -0x20 < d4 < 0x100, rl(rl(+0x58)) set 4, masks +0x2c; 0x0e at
+0x28, -0x30 < d4 < 0x120, rl(rl(+0x3a)) set 6; 7/8/9 at +0x20, -0x10 < d4 < 0x100,
rl(rl(+0x1c)) sets 5/6/4; 0x0f same, set 7, masks +0x26 (3); 0x29 at +0xc, -0x40 < d4 < 0x100,
rl(rl(+8)) set 4 (d4 = sy + 0x10). `marble_mo.py mo_list_26f3e` equals MAME for enemies except
an acid spawn tick, a slinky eating, bird mask x words. Arcade cap 60 MOs per frame.

Data (per race: slots + enemies): tiles 21, 333, 761, 545, 87, 255 (LZMA 15.9 KB total);
player death frames 170 tiles; frame headers 179, columns 383.

```sh
python3 romconv/internal/marble/tools/marble_enemy.py steelie TRACE RACE
python3 romconv/internal/marble/tools/marble_enemy.py spawn TRACE RACE
# camera driven silly race: MARBLE_CAMERA=800:1700 MARBLE_CAMERA_SY=240 (mame_trace.lua)
```

### Steelies in the main tick model (Phase 4b-1)

All from Marble Love TS, checked against MAME tick start traces. `marble_enemy.enable(m)`
(`m.enemies`) adds to `marble_sim.main_tick`: the spawner 0x15a12 in `camera_bands_144e4`
before the slots, 0x158cc for both steelies after the players, 0x1bc88 inside 0x121b8 and the
non-player landing 0x25c74. `marble_enemy.setup(m)` turns on deaths, slots, sprites, the draw
list and steelies; the steelie checks run `marble_mo.boxes_and_sort` (0x26f3e head) after
each tick.

- Camera order (`refresh-helper-13ee6.ts`): 0x144e4 runs with the old camera long still in
  0x40097c, the new one is stored after it. A steelie spawned in the tick projects (0x1b9cc)
  with the old camera. `main_tick` follows this.
- Spawn 0x15a12 (`scroll-sub-15a12.ts`): per entry of 0x22706[race] the free slot search runs
  first (highest free of 0x1effe; none free ends the scan), then "no live steelie with this
  node list" (ROM long entry +2; the TS reads it from work RAM, a TS bug) and the band rule.
  Init: +0x6e = +0x72 = node list, +0x1b zone, +0x76/+0x78 band (0x2276e: hi - 16), position
  node centre (cell << 19 + 0x40000), z = 0x1cc62, state 0x20, +0x6c = 0xffff, masks cleared,
  0x1b9cc, draw list insert type 2 (index +0x19). Then removals of steelies whose band range
  is left (0x15bd0(a, 0, 1)).
- Non-player embedded (> 16 px below the floor) or fallen (> 128 px) in 0x121b8
  (`helper-121b8.ts`): 0x15bd0(a2, 1, 1), no 0x25bae.
- 0x15bd0 (`state-sub-15bd0.ts`): clear: +0x18 = 0, draw list removal 0x18f46 (type 2); score:
  0x285b0(player, 3) for every player not in +0x18 0/2. 0x285b0 (`helper-285b0.ts`): +0xbc +=
  s16 0x23cd4[k] (0x28608, flag 0x40039c), +0xd4 = 0x23cf6[k], +0x68 = 0, +0x69 = 0xff,
  +0x70 = 0, +0xd8 = 1.
- Shattering steelie (+0x18 == 2, `sub-158f6.ts`): 0x25fc2, 0x1b9cc with the refetch flag =
  low byte of the steelie address (nonzero for both), 0x1281c. At the list end 0x25fc2 sets
  +0x18 = 0 and removes the draw list entry.

Tick start traces (`mame_trace.lua` `MARBLE_TICK=start`, scratch `$TMPDIR/st`, place 900 and
input `en/inp1.txt` from 1000 unless noted). `MARBLE_DROP "frame:obj:x:y:z"` (new, test
harness) drops the player or a steelie airborne at a position to provoke landings.

| run | race | parameters | content | free run |
| --- | --- | --- | --- | --- |
| b0 | 1 | place 300:284:-82:1 | first steelie, patrol, hunt | 746 ticks equal |
| b1 | 1 | place 465:545:-120:7 | spawn, 2 collisions, chase/hunt/return | 745 ticks equal |
| a1 | 3 | place 520:428:-84:4 | spawn, collision, chase/return | equal until pistons (frame 1077) |
| u1 | 5 | place 644:660:-32:9 | spawn, collisions, player deaths | 735 ticks equal |
| d1 | 1 | b1, drop 1155 player 459:552:-110 | player breaks the steelie, shatter, removal, score | 272 ticks equal |
| d2 | 1 | b1, drop 1155 steelie 459:552:-80 | steelie lands 40 px, shatters | 272 ticks equal |
| d3 | 1 | b1, drop 1155 steelie 459:552:-104 | steelie lands, shatters | 272 ticks equal |
| d4 | 1 | b1, drop 1299 steelie 493:528:-110 | steelie breaks the player, stun 0x24 | 200 ticks equal |

The draw list compare skips types 4 and 0x0e (slinkies, acid: in MAME, not modelled). No run
covers a steelie occluded by terrain (steelie masks +0x38 stay 0 in every tick).

Steelie fixture `source/test/test_marble_steelies/marble_steelies_trace.h` (committed, derived
numbers only), `make_fixture.py stwrite`: initial work RAM blocks (the drop applied), per tick
`MarbleSteelieTick` (inputs, player as `MarbleDeathTick`, both steelies `MarbleSteelieObj
{live, mode, vis, x, y, z, sx, sy, anim, frame_hdr, shard_hdr, anger, node, mask[30]}`, MOs,
the draw list in link order).

| segment | ticks | events |
| --- | --- | --- |
| beginner b1 1015 | 120 | spawn, 2 collisions, chase/hunt/return |
| beginner d1 1155 | 70 | player breaks the steelie, shatter 63, removal, player dizzy |
| beginner d2 1155 | 85 | steelie lands, shatter 64, removal |
| beginner d4 1299 | 60 | steelie breaks the player (state 2), stun |
| ultimate u1 1031 | 120 | spawn, chase, player lost and respawn |
| aerial a1 965 | 55 | spawn, collision, chase/return |

```sh
S=$TMPDIR/st
python3 romconv/internal/marble/tools/make_fixture.py stwrite \
  source/test/test_marble_steelies/marble_steelies_trace.h 1:$S/b1/trace.bin:1015:120 \
  1:$S/d1/trace.bin:1155:70:400018:459:552:-110 1:$S/d2/trace.bin:1155:85:4009a4:459:552:-80 \
  1:$S/d4/trace.bin:1299:60:4009a4:493:528:-110 5:$S/u1/trace.bin:1031:120 \
  3:$S/a1/trace.bin:965:55
python3 romconv/internal/marble/tools/marble_enemy.py sfixture \
  source/test/test_marble_steelies/marble_steelies_trace.h   # replay, no trace needed
```

## Open questions

- Level objects: unported tags (0x0a-0x0d, tubes 0x12-0x16, 0x20-0x27), zone triggered
  slots (state 4, 0x13966/0x13a1c, aerial), playfield patch decode of tag 0x19 rows. Section
  Level objects. Slot sprites: section Level object sprites (masks over slot MOs behind the
  marble not seen in traces).
- Enemies (section Enemies): slinky states 1/5/6 (0x14a28-0x14bba, not in Marble Love), two
  player target choice 0x15fe6, no trace of acid/bird/piston hits or of a steelie occluded by
  terrain (masks +0x38).
- Deaths (section Player states, deaths, RNG): states 1/2/4 modelled and ported. Not
  modelled: states 3 (catapult), 5/7/9/0xb (enemies), 8 (time out, 0x258a8), 0xa (gate,
  0x13d38 writes +0x38), practice landing markers (type 0x2a, 0x12532, score 0x285b0),
  aerial piston hook 0x186ac in 0x13966, the race timer and other vblank IRQ work, two
  players, the main loop countdowns 0x400768/0x4003b8 (aerial deaths diverge once 0x13966
  sets 0x400768 = 0x3c). Not ported to C++: zone hook 0x13966 (silly/aerial respawn: sound
  latches, 0x400768, zone wait slot restart via the slot script 0x12896), dots 0x1c014 /
  0x25b40.
- Goal (state 6, section Player states, deaths, RNG): the race end flow after it, and the
  +0x57 wrap (out of play again every 256 ticks) is unchecked against MAME. The camera
  ignores the marble (sy only from states 0/1/3/4/5).
- Object/enemy scripts, Phase 4. Sound command protocol, Phase 5.
