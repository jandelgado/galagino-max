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
- Palette: IRGB_4444, 1024 entries. Display colour (converter `marble_data.irgb_hw`): each
  4 bit component times the intensity table 0, 3, 4, ..., 0x11 (intensity 1 is dark grey,
  e.g. the aerial ramp 0x1eee = 42 grey), as the user's reference screenshot shows on every
  colour; MAME 0.289's linear intensity (`_irgb`, kept for trace screenshots) makes those
  greys almost black. Playfield colours 0x200 + colour_set * 32 + pixel,
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
  approximates the main loop (rng(0x100) on its wait tick). The race timer and the game
  flow: section Game flow. Not modelled: other IRQ work, countdowns 0x400768/0x4003b8. Stick as trackball, deltas per tick, k =
  `MARBLE_STICK_K` (9, about MAME's keyboard trackball speed; tune on the device): right c7
  = c6 = -k, left +k, +k, down c7 = +k, c6 = -k, up c7 = -k, c6 = +k; two directions add,
  scaled by 181/256 (about 1.4k on the axis both use). EXTRA held: no input (EXTRA + UP/DOWN
  is volume). Logic ticks run only while the race is on (`marble_flow::frame` returns LOGIC);
  a race load (`loadRace`): cleared work RAM image with the carried bytes, race start, camera
  and pan reset.

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
   Aerial zone 4 entry (0x13892): zone wait slots (state 4) removed, open gates 0x0b/0x0d
   (state 2) reclosed (anim 0x1d752/0x1d798, script restart). A changed zone runs the zone
   hook 0x13966 (section Pistons).
8. Height h = 0x1cc62 at the new position. Edge 0x160f6: a lower neighbour edge starts
   teetering (+0x36 = 1, vz = -0x6000), z more than 6 px above h starts a fall (+0x36 =
   2). Teetering: of the edges in +0x37 the one with the smallest 0x2398c magnitude wins
   (0x165ec `cmpa.w d2,a1; ble`; Marble Love keeps the largest), z += its height - magnitude
   when positive (aerial piston pits, frame 1249 of `$TMPDIR/ps/p1`). |z - z_old| > 8 px with vz <= 0 also starts a fall (sound 0x45).
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

- 0 roll (0x256d2): +0xcb set (time bonus event 0x1bbaa), on the ground and +0x58 0, 0x34
  or 0x35: state 8 (section Game flow); else 0x253bc, 0x17f66, 0x121b8.
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
+0x57 wraps to 0xff, so this repeats every 256 ticks while state 6 lasts (the race end ends
it first: 0x251de sets 0x400390 = 3 in the first tick out of play, section Game flow). Goal
entry 0x137a4: the first player at the goal (0x4003a4 = 0xff, set by 0x10504) scores
0x285b0(race * 2 + 3) (1000-6000) and the timer stops (+0x6d = +0x6e, +0x6e = 0xff).

Race start: `start_259b4` / `start_race` end at a `main_tick` boundary (the first tick runs
0x253bc/0x17f66); `tick()` users add `snapshot_start`. `begin_race` = the remake's
beginRace (start_race, projection, masks).

Remake (`marble_physics.cpp`, `marble_rng.cpp`): handlers of states 0, 1, 2, 4, 6, 0x25bae
(2, 4), 0x25fc2, 0x2591a, 0x262b2/0x2637a with 0x17cb8, 0x13ade, 0x26214, race start rng.
Score wobble 0x25416 (+0xd8 while a 0x285b0 score shows) ported with the goal points (Phase
3a). Not ported: dots 0x1c014/0x25b40, zone hook 0x13966, sounds. Host test `test_marble_deaths`: the death fixture per
tick (player fields, +0x5a, frame headers, masks, RNG; score, distance, death count at
segment ends, from the traces); race start RNG; start fixture `marble_start_trace.h`
(`make_fixture.py rwrite`, model only, all modelled enemies on since Phase 4b-4b: the silly
start spawns the minis, which use the RNG): per race beginRace + 60 ticks of stick input, and a
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
walls. Ported: 0x05, surfaces, walls, all tubes 0x12-0x16 and 0x20-0x27 (section "Tubes").

Validation (`marble_sim.py slots TRACE RACE`: slots, dynamic codes and slot globals never
copied from the trace after the first race snapshot; camera long and enemy scratch are):
all slot fields and all 128 dynamic words equal MAME every tick, and the marble physics on
them, in 20 traces, 16757 ticks: practice 6 (3 random input, 2 start pen input, 1 camera
driven; 13 slot wall contacts, physics segments up to 934 ticks), beginner 3 + 1 camera
driven, intermediate 3 + 1 camera driven (physics segment 528), silly 2, ultimate 2 + 1
camera driven, aerial 1 short (camera traces: `MARBLE_CAMERA=800:1600` in
mame_trace.lua, spawns and removals over the whole race, beginner 46 and ultimate 37
dynamic code changes). The second aerial trace differs only where gates 0x0b/0x0d act.
Not covered by a trace: the intermediate wave (tag 6), zone triggers (tubes: section "Tubes").

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
| aerial | 5 flags (2), 0x0a catapult (1), 0x0b/0x0d gates (6), 0x0c bounce boxes (4) | gates bands 3-30, catapult cell (63, 70), boxes x cell 105 | catapult set 6, gates set 3, boxes set 7; gate lists from the pc-entered scripts 0x1d766/0x1d7ac/0x1d752/0x1d798 |
| silly | 5 flags (2) | (152/184, 144) | |
| ultimate | 5 flags (5) | (704-736, 704-784), z -32 | |

Types 10-13 (aerial catapult, gates, bounce box): 0x26f3e culls 10 at d4 <= -0x40 or >=
0x120 (frame | 0x3000), 11-13 at d4 <= -0x20 or >= 0x100 (11/13 | 0x1800, 12 | 0x3800); a
gate (11/13) adds two priority MOs code 0x500 height 7 (x = d5 - 8 + 8k, y = d4 - 0x40 (0x0d:
+ 4) +- 4k, 0x0d hflip) that hide earlier MOs in its mouth. `marble_mo.slot_mos` equals MAME
MO RAM on the aerial traces a5, a8, a9, a10, a13 (17425 slot draws). The remake draws the
frames (sprite set 2, shared by intermediate and aerial, `SHARED_SETS`); the priority MOs are
not drawn (remake masks hide only their owner), so the marble shows over a gate mouth. Tags
>= 0x10 are skipped by 0x26f3e.

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

### Phase 4b-2 data (slinkies)

Same converter run; flash 3773 B (prog blocks 1412, slinky tiles 1355, frames 204, columns
684, header ids 102, palette 16).

- The 51 slinky frames (13 animation lists `marble_mo.SLINKY_LISTS`, 161 tiles, MO set 4)
  join the shared death set, after the player death frames (frame ids >=
  `MARBLE_DEATH_FRAMES` = 44): one copy instead of one per race set (in all three race sets
  they cost 8180 B; only in the slinky races they would split shared sets). Death set 95
  frames, 380 tiles; `SPRITE_TILES` = 380 + 535 = 915 (29280 B Arena).
- Palette: set 4 is equal in the slinky races 1, 2, 5 (converter check), so it is a third
  row of `marble_death_pal` (fixed, not per race).
- Program ROM blocks: collection replays the slinky fixture and the slinky race sweep
  (`marble_slinky.sweep`, all races); whole tables: spawn table 0x2257a (pointers, entries,
  0xff end), every entry's waypoint list to its 0xff end, 0x1f006, the animation lists, the
  lunge table 0x2255a (0x20 B). The runs also read slinky frame header bytes 0-7 (mask box,
  contact box, draw list box).

### Phase 4b-3 data (acid)

Same converter run; flash 1794 B booked to 4b-3 (prog blocks 510, acid tiles 428, frames 140,
columns 630, header ids 70, palette 16); the steelie and slinky runs now run with acid on and
read acid tables too, so 4b-1/4b-2 prog blocks grew by 1044 B (all prog blocks +1554 B).

- The 35 acid frames (anim lists `marble_mo.ACID_LISTS` 0x20ea4, 0x20ec4, 0x20ee4, 0x20f24,
  MO set 6, 69 tiles) join the shared death set. Their headers (0x21786-0x2191e) sort between
  the player frames and the slinky frames: ids >= `MARBLE_DEATH_FRAMES` (44) are enemy frames,
  acid first. Death set 130 frames, 449 tiles; `SPRITE_TILES` = 449 + 535 = 984 (31488 B,
  Arena block B).
- Palette: set 6 is equal in the acid races 2 and 5 (converter check), fourth row of
  `marble_death_pal`.
- Program ROM blocks: collection replays the acid fixture and the acid race sweep
  (`marble_acid.sweep`, all races); whole tables: spawn table 0x23d4a (pointers, entries, 0xff
  end), the random path lists 0x23d62-0x23d99, every path to its 0xff end, 0x1f07a, the
  animation lists.

### Phase 4b-4a data (birds)

Same converter run; flash 574 B booked to 4b-4a (prog blocks 34, bird tiles 344, frames 40,
columns 120, header ids 20, palette 16).

- The 10 bird frames (anim lists `marble_mo.BIRD_LISTS` 0x224ca, 0x224d6, 0x224e2, 0x22546,
  MO set 7, 35 tiles) join the shared death set. Their headers (0x22452-0x224be) sort after
  the slinky frames: enemy frames are acid, slinkies, birds by header address. Death set 140
  frames, 484 tiles; `SPRITE_TILES` = 484 + 535 = 1019 (32608 B, Arena block B).
- Palette: set 7 of the silly race (the only bird race), fifth row of `marble_death_pal`.
- Program ROM blocks: collection replays the bird fixture and the bird race sweep
  (`marble_bird.sweep`, all races); whole tables: 0x1f0ba, spawn cells 0x244f6, the animation
  lists.

### Phase 4b-4b data (minis)

Same converter run; flash 1141 B booked to 4b-4b (prog blocks 86, mini tiles 359, frames 208,
columns 336, header ids 104, palettes 48).

- The 52 mini frames (anim lists `marble_mo.MINI_LISTS`, MO sets 5/6/4) join the shared death
  set. Their headers (0x2192a-0x21f66) sit around the slinky frames: enemy frame ids by
  header address are acid, minis, slinkies, minis, birds (tests bound the slinky and acid
  ranges by their last headers). Death set 192 frames, 515 tiles; `SPRITE_TILES` = 515 + 535
  = 1050 (33600 B, Arena block B).
- Palettes: silly race sets 5, 6, 4 (types 7, 8, 9) as `marble_death_pal` rows 5-7.
- Program ROM blocks: collection replays the mini fixture and the mini race sweep; whole
  tables: 0x1f096, step tables 0x244b6/0x244d6, cell bitmap 0x24496, the animation lists.

### Phase 4b-5 data (pistons)

Same converter run; flash 635 B booked to 4b-5 (prog blocks 528, piston tiles 43, frames 16,
columns 24, header ids 8, palette 16).

- The 4 piston frames (anim list 0x21342, headers 0x21312-0x21336, MO set 4) join the shared
  death set. Their headers sort before the player death frames, so they take ids 0-3:
  `MARBLE_DEATH_FIRST` (4) is the first player frame id, `MARBLE_DEATH_FRAMES` (44) the
  player frame count (the death set header table must stay sorted, `deathFrame` searches
  it). Death set 196 frames, 525 tiles; `SPRITE_TILES` = 525 + 535 = 1060 (33920 B, Arena
  block B).
- Palette: aerial set 4 equals no other row: `marble_death_pal` row 8 (`SET_PISTON`),
  `SET_PAL` widened to 4 bits.
- Program ROM blocks: collection replays the piston fixture and the piston race sweep; whole
  tables: timers 0x242c6/0x242f6, variant pointers 0x243e6/0x243f6 and their 8 tables,
  cells 0x24406/0x2442a, tops 0x2444e, the animation list.

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

C++ (`marble_enemies.{h,cpp}`, 1:1 port of `marble_enemy.py`): `marble_objects::cameraBands`
(0x144e4) calls `marble_enemies::cameraBands` before the slot spawner, so the race start and
every band step spawn steelies; `marble.cpp` restores the old camera long around it (0x13ee6
order). `marble_physics::physics_121b8` is object generic: pair collision and the 0x1239c
cell refresh for every object, zones and gravity for players only, non-player landing and
loss in `marble_enemies`; `anim_25fc2` frees a shattered steelie and its draw list entry.
Host test `test_marble_steelies`: fixture replay (player, both steelies, draw list), race
sweep of all races (spawns at table bands, counts as `marble_enemy.sweep`: beginner 4/5,
aerial 3/3, ultimate 2/1 spawns/removals), shatter end and race switch. Old fixtures stay
exact with steelies always on (model check with `enable` forced).

Drawing: `marble_drawlist::build` publishes type 2 at its list position as `marble_mo.other_mos`
draws it, only while +0x1c (the arcade cull 0x1281c, also the masks' condition; the remake
margin does not apply, as for the marble): frame 0x21f36 from the race set (`Obj::set` 5), or
while shattering the death frame rl(+0x5a) with `SET_DEATH | SET_RACE5` (race palette set 5),
the +0x38 masks right after it, the overlay rl(+0x62) (`SET_DEATH | SET_SHARDS`). Host tests:
`test_steelie_objects` (every fixture tick: columns, palette sets, masks as MO words equal to
MAME), `test_steelie_edges` (reference composite at every view edge, guarded tile buffer),
object count of every sweep tick within `OBJS`. Flash after Phase 4b-1: 4080989 B (+7004).

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

### Slinkies in the main tick model (Phase 4b-2)

Marble Love TS (`state-sub-14c46.ts`, `sub-14966.ts`, `helper-15148.ts`,
`state-dispatch-15460.ts`, `state-sub-15670.ts`, `nearest-neighbor.ts` V2,
`sprite-coords-jsr-150d0.ts`, `fun-264aa.ts` mode 2, `script-slot-bbox-test-14e92.ts`,
`refresh-frame-10fce.ts` player states 5/7), except 0x14a28-0x14bba (not in the TS; ported
from the disassembly by user ruling 2026-10-12, that block only). Model
`tools/marble_slinky.py`, `marble_slinky.enable(m)` (`m.slinkies`): spawner 0x14c46 after
0x15a12 in `camera_bands_144e4`, 0x1493c after the steelies, 0x14e92 for players in 0x121b8
after 0x1bc88 (races 1/2/5), player states 5 (0x257ba) and 7 (0x25812) in `object_253ec`.

Record 0x401302 + 0x60 k (table 0x1f006, +0x19 = k): v +0/+4, pos +0xc/+0x10/+0x14, +0x18
live, +0x1a state, +0x1b zone, +0x1c/+0x20 marble velocity at a push, +0x24 tick, +0x25 ticks
per frame (2 in states 0/4, else 1), +0x26 frame step (1, -1), +0x27 lunge tick, +0x28 screen
(x, y words as slots), +0x2c masks (5 x {code, x, y}), +0x4a next waypoint, +0x4e waypoint
list (4 byte records cell x, y, next record offset, pad; 0xff end), +0x52/+0x54 band, +0x56
prey (index into 0x1eff6), +0x58 frame cursor, +0x5c anim list (ROM longs, -1 end).

- Spawn 0x14c46: as 0x15a12 (highest free slot first; none free ends the scan; skip if a live
  slinky has the same list +0x4e), position = first waypoint centre, z = 0x1bb08 + 0x1cc62(1),
  state 0, anim 0x20c18, +0x26 = 1, +0x25 = 2, 0x150d0, draw list insert type 4. Removal: band
  left, +0x18 = 0, 0x18f46. Tables: beginner 5-30, 5-31, 6-34 (zones 3, 3, 4), intermediate
  1-24 x2 (zones 1, 2), ultimate 0-25 x2, 6-32 x2 (zones 4, 4, 5, 5).
- Tick 0x14966: +0x24 += 1; at +0x25 the cursor steps by +0x26 records; at the list end (or
  back at its start) it restarts, walking slinkies (states 0/3, step > 0) move by v, and the
  state machine 0x15148 runs; state 2 runs it every tick. States 1/5/6 then run the lunge
  0x14a28; always 0x150d0 (screen +0x28, masks 0x264aa mode 2 at +0x2c: box from the frame
  header bytes 0-3, no explicit occluders 0x1ea2a, +0x1c untouched).
- 0x15148: 0/3 walk (cell == list start and anim not idle: stop, idle anim, next = record
  after it; cell == next waypoint: next = its successor; then aim 0x15460; walking: prey
  search 0x15670), 1 chase (prey > 0x100 on an axis: 6; octant < 0x70: catch, prey position =
  slinky, prey state 7, slinky 5, sound 0x33; < 0xc0: prey v = 0x400 * (d / octant) >> 4;
  else and after the pull: 6), 2 pushed (0x25e7c mode 2 friction; v 0: 4; step changing the
  ground height undone, up 1, down 4), 4: 1, 5/6: position snapped to the cell centre, 5 sends
  the prey lost (0x15884, +0x57 = 0x65, 0x25bae(4)), then 3 with next = nearest waypoint
  (0x14dec).
- 0x15460 anim per state: 0/3 one axis step of 8 towards the next waypoint (x first when v x
  is 0), anims 0x20c48/6c/90/b4, idle 0x20c18; 1 0x20cd8 (+0x27 = 0); 2 0x20d64 after the push
  anim plays out (+0x26 -1 for the reverse half); 4 by the push velocity +0x1c/+0x20 0x20dd8/
  e14/dec/e00; 5 0x20e28; 6 0x20d6c. Then cursor = list start, +0x24 = 0.
- 0x15670 (walking): prey = the last rolling player (+0x18 1, same zone, |vx| + |vy| >
  0xc000, on the ground, state 0/1/5, not prey of another chasing slinky); +0x56 = its number;
  octant distance 0x180-0x280 (24-40 px): state 1. Two candidate players call 0x15fe6
  (unported: `fail`).
- Lunge 0x14a28 (+0x27 in 6..0x25, then +0x27 += 1 always): z = ground + s8 0x2255a[+0x27 -
  6] << 16 (the slinky rears up). Prey live, same zone, on the ground at exactly this ground
  height, state 0/1/5/7: v = d >> 4, then v += ((0x800 * (d / octant) >> 4) - v) >> 5 (d in
  16.16, octant of the 1/16 px deltas, unsigned); else v = 0. Step by v, undone if the ground
  height under the new cell differs.
- Contact 0x14e92 (players, races 1/2/5, first hit only): marble box x/y +-3, z +0..+0xe vs
  slinky box (frame header bytes 4-7, -4, -4, 8, 8 for an empty anim; z +0..+0x10). Slinky in
  0/3: state 2, v = +0x1c/+0x20 = marble v. Chasing (1/6) its own prey: nothing more. Else
  +0x56 = player (unless player or slinky in 5); marble back to 0x400684/0x400688, v = 0; not
  in 5: +0x5f = 0, +0x60 = 2, +0x5a = 0x20fb6 (dizzy) or 0x20faa (sound 0x39); state 5, +0x56
  = 0x32 unless already 5/7.
- Player 5 (0x257ba): 0x25fc2, 0x253bc, 0x17f66, 0x121b8, then +0x56 counts down; at 0 state
  0, +0x5a = 0, sound pair, dots 0x1c014. Player 7 (0x25812): 0x253bc, +0x1c = 0.
- Marble Love catch test `dist1 > 0x70` contradicts its own quoted `ble` (catch below 0x70);
  the model uses < 0x70 and matches every catch.

Tick start traces (`$TMPDIR/sl`, `$TMPDIR/st/run.sh`, place at frame 900, input from 950 or
1000):

| run | race | place / input | content | free run |
| --- | --- | --- | --- | --- |
| b1 | 1 | 404:356:-120:3 | push, stop, chase, lunge, catch, player 5/7/4 | 399 ticks equal |
| b3 | 1 | 420:380:-120:3, alternating rolls | push, eat | 498 ticks equal |
| b6 | 1 | 404:396:-120:3, `120:2:3,300:-2:-3` | chase from walking (0x15670), lost prey 6, back 3 | 298 ticks equal |
| b7 | 1 | 404:396:-120:3, `120:3:2,300:-3:-2` | chase, 6, push 23 ticks, catch | 298 ticks equal |
| i1, i2 | 2 | 356:284:-64:1, 276:364:-64:2 | eat by each intermediate slinky | 398 ticks equal each |
| u1 | 5 | 320:400:0:4 | eat, acid alongside | 398 ticks equal |

Also equal: `slinky TRACE RACE` (0x1493c alone between snapshots) on 12 older traces (~6300
ticks), spawns/removals of cam1/cam2/cam5 (`spawncheck`, 34 events), st/b1 and st/u1 with
steelies. Old commands (fixture, objfixture, dfixture, sfixture, spritefixture) stay exact
with slinkies forced on. Acid (type 0x0e) shares the draw list sort; since Phase 4b-3 the
checks compare the whole list in order.

C++ (`marble_slinkies.{h,cpp}`, 1:1 port of `marble_slinky.py`, internals in namespace
`marble_slinkies`: the host tests compile every module into one unit, where the steelie
constants of `marble_enemies.cpp` would hide same-named slinky ones): `marble_enemies` forwards
`init`, `cameraBands` (after the steelies), `update` (after the steelies) and
`contact_14e92`, which `physics_121b8` calls for players after the pair collision.
`marble_physics` gains states `EATEN` (0x257ba) and `HIDDEN` (0x25812), `cellHeight`
(0x1bb08 + 0x1cc62(1)) and `drag` (0x25e7c mode 2); `marble_occlusion::slinky` is 0x264aa
mode 2. `marble_enemy.setup` (model) turns slinkies on too, so the steelie fixture RAM blocks
hold the slinky records (fixture regenerated). Host test `test_marble_slinkies`: fixture
replay (player, steelies, slinkies, draw list), race sweep (spawns at table bands, counts as
`marble_slinky.sweep`: beginner 9/9, intermediate 6/6, ultimate 8/10, most live 3/2/4), a
slinky eating the still marble in each slinky race (5, 7, 4, then rolling) and a race switch
while the player is held.

Drawing: `marble_drawlist::build` publishes type 4 at its list position as
`marble_mo.other_mos` draws it: origin +0x28 (x) and camera top + 240 - +0x2a, culled as the
arcade (-0x20 < sy + 0x10 < 0x100) widened by the remake margin, frame rl(+0x58) from the death
set with `SET_DEATH | SET_SLINKY` (`marble_death_pal` row 2), the +0x2c masks right after it.
The player in state 5 draws its +0x5a frame as in state 1; in state 7 +0x1c is 0 (hidden).
Host tests: `test_slinky_objects` (every fixture tick at the arcade cull: columns, palette set
4, masks as MO words equal to MAME; 1355 drawn slinky ticks, 35 mask MOs),
`test_slinky_edges` (reference composite at every view edge, guarded tile buffer). The
sprite fixture test compares level objects only (its start RAM holds slinky entries it does
not update). Flash after Phase 4b-2: 4090209 B (+9220 over 4b-1: data 3773, code ~5.4 KB).

Slinky fixture `source/test/test_marble_slinkies/marble_slinkies_trace.h` (`make_fixture.py
slwrite`): RAM blocks, per tick `MarbleSlinkyTick` (inputs, player, both steelies, the four
slinkies `MarbleSlinkyObj {live, state, x, y, z, sx, sy, tick, next, prey, cursor, frame_hdr,
mask[30]}`, MOs, draw list without acid).

| segment | ticks | events |
| --- | --- | --- |
| beginner b7 1231 | 130 | chase, lost prey, push, stop, catch, player 5/7 |
| beginner b6 1301 | 95 | lost prey, back, second chase, player lost |
| intermediate i1 990 | 105 | stop, chase, catch, player 5/7/4, respawn |
| ultimate u1 902 | 100 | catch, player 7/4, acid in the list |
| beginner st/b1 1015 | 60 | steelies and three walking slinkies |

```sh
S=$TMPDIR
python3 romconv/internal/marble/tools/make_fixture.py slwrite \
  source/test/test_marble_slinkies/marble_slinkies_trace.h 1:$S/sl/b7/trace.bin:1231:130 \
  1:$S/sl/b6/trace.bin:1301:95 2:$S/sl/i1/trace.bin:990:105 5:$S/sl/u1/trace.bin:902:100 \
  1:$S/st/b1/trace.bin:1015:60
python3 romconv/internal/marble/tools/marble_slinky.py slfixture \
  source/test/test_marble_slinkies/marble_slinkies_trace.h
python3 romconv/internal/marble/tools/marble_slinky.py slinkies TRACE RACE FROM:TO
```

### Acid in the main tick model (Phase 4b-3)

Marble Love TS only (`string-range-dispatch-17346.ts`, `dispatch-strings-17230.ts`,
`string-step-1725a.ts`, `entity-waypoint-step-1d1ec.ts`, `sub-1d242.ts`, `sprite-coords.ts`
0x1778e, `string-viewport-hit-175c8.ts`, `string-target-step-176d2.ts`,
`refresh-frame-10fce.ts` JT[9]); no TS gap, no TS/MAME disagreement. Model
`tools/marble_acid.py`, `marble_acid.enable(m)` (`m.acid`, on in `marble_enemy.setup`):
spawner 0x17346 after 0x14c46 in `camera_bands_144e4`, 0x17230 after the slinkies, 0x175c8 for
players in 0x121b8 right after 0x14e92 (a hit runs 0x1bab2), player state 9 (0x2584e) in
`object_253ec`.

Record 0x401482 + 0x42 k (table 0x1f07a, +0x19 = k): v +0/+4, pos +0xc/+0x10/+0x14, +0x18
live, +0x1a 0, +0x1b zone, +0x24 tick, +0x25 ticks per frame, +0x28 screen (x, y words as
slots), +0x2c path cursor, +0x30 path (4 byte records cell x, y, next record, pad; 0xff end),
+0x34/+0x36 band, +0x3a frame cursor, +0x3e anim list (ROM longs, -1 end).

- Spawn 0x17346 (table 0x23d4a[race], entries as slinkies): per entry, skip if a live acid has
  the entry's path (0x1730c, +0x30 vs the ROM long), then the band rule. Long 0: rng(2) picks
  0x23d62/0x23d66 (zone byte 5: one path) or 0x23d6a/0x23d82 (six paths), else the long is
  the path. Each path takes the highest free slot; none free ends the walk. Init: position
  = first cell << 19 (cell corner, no +0x40000 centre), z = 0x1bb08 + 0x1cc62(1) (fixed
  afterwards), v = 0, 0x1d1ec, 0x1778e, draw list insert type 0x0e. Removal as slinkies.
  Tables: intermediate 14-36 random single (zone 5), 6 x 17-42 random group (zone 6; the
  first fills six slots, the next fills the last free one or ends the walk); ultimate 0-25 x2
  (zone 3), 6-32 x2 (zone 0x1e).
- Tick 0x1725a: +0x24 += 1; at +0x25: +0x24 = 0, +0x25 > 2 becomes 1, cursor +0x3a steps;
  at the list end it restarts, pos += v (8 px), 0x1d1ec. Always 0x1778e (screen +0x28, the
  0x150d0 maths without masks).
- 0x1d1ec: on the cursor's cell, cursor = path + next * 4; then 0x1d242: one axis step of 8
  px towards the cursor's cell (y first when v x != 0, else x first), v = step << 16, anim
  0x20ec4 down / 0x20ee4 up / 0x20ea4 right / 0x20f24 left (also standing), +0x24 = 0, +0x25 =
  2, or 1 while a live player (+0x18 == 1) shares the acid's zone and that zone is 6.
- Contact 0x175c8 (players, races 2/5, first hit only, no height test): marble x/y +-3 vs
  the box of frame header rl(rl(+0x3a)) bytes 4-7 (-2, -2, 12, 12 for an empty frame): player
  0x25bae(9) (v 0, +0x5f 0, +0x60 4, anim 0x21062, state 9), sound 0x5e, player +0x58 = acid
  number, acid +0x25 = 0x1c (its frame holds 28 ticks, then +0x25 = 1).
- Player 9 (0x2584e): 0x176d2 (x, y words step 1 px towards the acid box centre, fraction
  cleared), 0x25fc2 (list end: 0x25bae(4), lost and respawn), 0x1b9cc(1), 0x1281c.
- 0x17cb8 (respawn spot check) does not scan acid.
- Mid tick traces (snapshot at the player's 0x121b8 entry, the older runs): the tick's
  rng(0x100) and the acid update with the snapshot's camera are still due, `spawncheck` and
  `acid` take `mid`.

Checks: `spawncheck` cam2, cam5, e2 (mid), u1: 44 spawn/removal steps equal; `acid` (0x17230
alone between snapshots) cam2, cam5, e2, u1: 1248 ticks equal; `acids` free runs below; the
sweep (all races, all modelled enemies): intermediate 21/21, ultimate 8/10 acid
spawns/removals, most live 7/4. Old commands stay exact with acid on; the slinky fixture u1
segment is regenerated with the C++ port (its RAM blocks then hold the acid records).

Tick start traces (`$TMPDIR/ac`, `$TMPDIR/st/run.sh`, no input):

| run | race | place | content | free run |
| --- | --- | --- | --- | --- |
| ia | 2 | 1500:530:550:-64:6, 1640:516:548:-64:6 | single spawn, group spawn, hit, dissolve, respawn | 296 ticks equal |
| ua | 5 | 900:320:400:0:4, 1150:380:316:0:4 | slinky eat with acid, dissolve, respawn | 396 ticks equal |

C++ (`marble_acid.{h,cpp}`, 1:1 port of `marble_acid.py`, internals in namespace
`marble_acid`): `marble_enemies` forwards `init`, `cameraBands` (after the slinkies), `update`
(after the slinkies), `contact_175c8` (`physics_121b8` calls it for players after
`contact_14e92`, a hit refreshes the cell) and `target_176d2`. `marble_physics` gains state
`DISSOLVING` (0x2584e) and its 0x25bae entry. Host test `test_marble_acid`: fixture replay
(player, acid, draw list in order), race sweep (spawns at table bands, counts as
`marble_acid.sweep`), a dissolve on the first acid of each acid race (9, 4, then rolling) and
a race switch while dissolving.

Drawing: `marble_drawlist::build` publishes type 0x0e at its list position as
`marble_mo.other_mos` draws it: origin +0x28 (x) and camera top + 240 - +0x2a, culled as the
arcade (-0x30 < sy + 0x10 < 0x120) widened by the remake margin, frame rl(+0x3a) from the death
set with `SET_DEATH | SET_ACID` (`marble_death_pal` row 3), no masks. The dissolving player
draws its +0x5a frame (0x21062) as states 1/2/5. Host tests: `test_acid_objects` (every
fixture tick at the arcade cull: columns, palette set 6 as MO words equal to MAME; 1605 drawn
acid ticks; acid crawling the same cells with the same frame are matched by index order),
`test_acid_edges` (reference composite at every view edge, guarded tile buffer). The sprite
sweep test counts level objects only (its model sweep has no enemies). Flash after Phase
4b-3: 4095473 B (+5264 over 4b-2).

Acid fixture `source/test/test_marble_acid/marble_acid_trace.h` (`make_fixture.py acwrite`):
RAM blocks, per tick `MarbleAcidTick` (inputs, player as `MarbleDeathTick`, the seven acids
`MarbleAcidObj {live, x, y, z, sx, sy, tick, path, cursor, frame_hdr}` with their MOs, the
draw list in order).

| segment | ticks | events |
| --- | --- | --- |
| intermediate ia 1600 | 19 | single spawn, crawl |
| intermediate ia 1642 | 150 | group spawn (rng), hit, dissolve 28, lost 24, respawn |
| ultimate ua 1153 | 110 | dissolve, lost, respawn, slinkies and steelie alongside |
| ultimate ua 902 | 60 | slinky eat (5/7) with four acids crawling |

```sh
S=$TMPDIR/ac
python3 romconv/internal/marble/tools/make_fixture.py acwrite \
  source/test/test_marble_acid/marble_acid_trace.h 2:$S/ia/trace.bin:1600:19 \
  2:$S/ia/trace.bin:1642:150 5:$S/ua/trace.bin:1153:110 5:$S/ua/trace.bin:902:60
python3 romconv/internal/marble/tools/marble_acid.py acfixture \
  source/test/test_marble_acid/marble_acid_trace.h
python3 romconv/internal/marble/tools/marble_acid.py acids TRACE RACE FROM:TO
python3 romconv/internal/marble/tools/marble_acid.py spawncheck TRACE RACE [mid]
```

### Birds in the main tick model (Phase 4b-4a)

Marble Love TS (`scroll-range-144e4.ts` mode 4, `state-sub-19baa.ts`, `state-sub-19a40.ts`,
`marble-cell-dispatch-19e42.ts`, `bbox-hit-test-19d94.ts`, `refresh-frame-10fce.ts` JT[11]),
except 0x264aa mode 3 (TS no-op; ported from the disassembly by user ruling 2026-10-12, that
block only). Model `tools/marble_bird.py`, `marble_bird.enable(m)` (`m.birds`, on in
`marble_enemy.setup`): 0x400762 in `camera_bands_144e4` after the slot spawner (silly race),
0x19baa after the acid, 0x19d94 for players in 0x121b8 after 0x175c8 (then state 0xb leaves
0x121b8 before the zones 0x1365c), player state 0xb (0x25876) in `object_253ec`.

Record 0x4019f8 + 0x38 k (table 0x1f0ba, +0x19 = k): v +4 (y only), pos +0xc/+0x10/+0x14, +0x18
live, +0x1a state (0 walk, 1 stand, 2 landed or carrying), +0x1b anim loops left, +0x1c anim
cursor (ROM longs, -1 end), +0x20 screen (x, y words as slots), +0x24 tick, +0x25 ticks per
frame, +0x26 masks (3 x {code, x, y}).

- Flag 0x400762 (0x144e4, silly race): set when the band enters 3-0x1b, cleared when it
  leaves (signed band bytes). Level init clears it. Birds already live keep walking.
- Spawn 0x19a40 (0x19baa, flag set and 0x400010 & 7 == 0): two passes over the 5 cells of
  0x244f6 (x, y bytes: 57-63, 55-63, 49-58, 47-58, 41-58), at most one spawn per pass. A cell
  spawns if at most pass-many live birds stand in its x column (one: only if it is 4+ cells
  below the spawn row); lowest free slot, none free ends the scan. Init: cell centre (<< 19 +
  0x40000), z 0x3fd8, state 1, +0x25 8, anim 0x224ca, v -4 px, 0x19e42, draw list insert type
  0x0f.
- Tick 0x19baa: +0x24 += 1; at +0x25 the cursor steps; at the list end a bird not in state 2
  walks again (anim 0x224d6, state 0, +0x1b -= 1; at 0: v -2 px and no bird ahead in its
  column: +0x25 1, v -4 px, +0x1b rng(2) + 4; else +0x25 4, v -2 px, +0x1b rng(2) + 1); a bird
  in state 2 is removed (0x18f46). Walking (state 0): y += v; terrain (0x1bb08 + 0x1cc62(1))
  above z: y back to the 4 px grid of the old y, anim 0x224e2, +0x25 1, state 2 (landed).
  Always 0x19e42.
- 0x19e42: screen +0x20 (the 0x150d0 maths), masks 0x264aa mode 3 at cells x 0x29/0x31/0x39
  with y >= 0x34, else the 3 entries cleared.
- 0x264aa mode 3 (disassembly, A1 = bird): entries at +0x26 (3 cleared), 3 columns, column =
  (+0x20 + 3) asr 3, sy = +0x22 - 8, top = sy + 0x10; then the slinky path (no explicit
  occluders, +0x1c untouched).
- Contact 0x19d94 (players, silly race, every walking bird, no height test): marble point
  0x400690/0x400692 in [x - 6, x + 6) x [y - 4, y + 8): bird state 2, anim 0x22546, +0x25 4;
  player state 0xb, +0x57 0x66, sound 0x3e.
- Player 0xb (0x25876): 0x1b9cc(1), orbit 0x13ade (radius grows, `orbit_13ade` state 0xb
  branch); at its end +0x57 = 0x65, 0x25bae(4) (lost, respawn).
- Minis share the RNG and the draw list; their band (0x1d-0x38) does not overlap the bird
  band. The bird traces below were compared while no mini was live; since Phase 4b-4b the
  `minis` check runs them whole.

Checks: `birds` free runs below (masks of 0x264aa mode 3 included: 434 mask entries); the
sweep (all races, all modelled enemies): silly 32 spawns / 23 removals, most live 10, flag
only in the band. Old commands stay exact with birds on.

Tick start traces (`$TMPDIR/bd`, `$TMPDIR/st/run.sh`, no input; minis live before frame 1132
and again after 1363):

| run | race | place | content | free run |
| --- | --- | --- | --- | --- |
| b1 | 4 | 900:444:470:-44 | spawns, walk, landings, carry, orbit, respawn, removals | 115 ticks equal |
| b2 | 4 | 900:420:470:-50 (rolls to 461) | same, another bird carries | 114 ticks equal |

C++ (`marble_birds.{h,cpp}`, 1:1 port of `marble_bird.py`, internals in namespace
`marble_birds`): `marble_enemies` forwards `init`, `sillyBands` (new: `marble_objects::
cameraBands` calls it after the slot spawner), `update` (after the acid) and `contact_19d94`
(`physics_121b8` calls it for players after `contact_175c8`; a carried player leaves before
the zones). `marble_physics` gains state `CARRIED` (0x25876; replaces the `STATE_ENEMY`
placeholder) and exports `CARRIED_DAMAGE` (0x66); `marble_occlusion::bird` is 0x264aa mode 3.
Host test `test_marble_birds`: fixture replay (player, birds, masks, 0x400762, draw list in
order), race sweep (flag against the band, counts as `marble_bird.sweep`), a carry on the b1
spot (CARRIED, LOST, then rolling) and a race switch while carried.

Drawing: `marble_drawlist::build` publishes type 0x0f at its list position as
`marble_mo.other_mos` draws it: origin +0x20 (x) and camera top + 240 - +0x22, culled as the
arcade (-0x10 < sy + 0x10 < 0x100) widened by the remake margin, frame rl(+0x1c) from the death
set with `SET_DEATH | SET_BIRD` (`SET_PAL` widened to 3 bits, `marble_death_pal` row 4), the
+0x26 masks right after it. A carried player (state 0xb) draws its orbit stars as state 4; the
first carried tick shows the surface dots in MAME (0x1c014, not in the remake). Host tests:
`test_bird_objects` (every fixture tick at the arcade cull: columns, palette set 7, masks as MO
words equal to MAME; 639 drawn bird ticks, 208 mask MOs; orbit stars in 72 carried ticks),
`test_bird_edges` (reference composite at every view edge, guarded tile buffer). Flash after
Phase 4b-4a: 4097997 B (+2524 over 4b-3); VideoState unchanged (304 B).

Bird fixture `source/test/test_marble_birds/marble_birds_trace.h` (`make_fixture.py bdwrite`):
RAM blocks, per tick `MarbleBirdTick` (inputs, player as `MarbleDeathTick`, the ten birds
`MarbleBirdObj {live, state, x, y, z, sx, sy, tick, loops, cursor, frame_hdr, mask[18]}` with
their MOs, 0x400762, the draw list in order).

| segment | ticks | events |
| --- | --- | --- |
| silly b1 1132 | 115 | spawns, walk, landings (masks), carry 36, lost 24, respawn, removals |
| silly b2 1180 | 60 | carry by bird 0, lost, respawn, 9 birds live |

```sh
S=$TMPDIR/bd
python3 romconv/internal/marble/tools/make_fixture.py bdwrite \
  source/test/test_marble_birds/marble_birds_trace.h 4:$S/b1/trace.bin:1132:115 \
  4:$S/b2/trace.bin:1180:60
python3 romconv/internal/marble/tools/marble_bird.py bdfixture \
  source/test/test_marble_birds/marble_birds_trace.h
python3 romconv/internal/marble/tools/marble_bird.py birds TRACE RACE FROM:TO
python3 romconv/internal/marble/tools/marble_bird.py sweep
```

### Minis in the main tick model (Phase 4b-4b)

Marble Love TS (`scroll-range-144e4.ts` mode 4, `array-9-init-and-dispatch-18ffa.ts`,
`array-9-clear-and-dispatch.ts`, `refresh-helper-1912c.ts`, `object-type-dispatch-194ba.ts`,
`state-sub-1960e.ts`, `sub-19692.ts`, `state-sub-198bc.ts`, `sub-19976.ts`, `sub-1937c.ts`,
`helper-1924e.ts`, `sprite-coords.ts` 0x199d6), except 0x1973c (TS calls 0x198bc directly) and
0x1953e (TS writes one list per type) from the disassembly by user ruling 2026-10-12. Model
`tools/marble_mini.py`, `marble_mini.enable(m)` (`m.minis`, on in `marble_enemy.setup`): band
spawn/clear in `camera_bands_144e4` before the bird flag, 0x1912c after the acid, 0x1924e for
players in 0x121b8 after 0x175c8 (before 0x19d94).

Record 0x401890 + 0x28 k (table 0x1f096, +0x19 = k): v +0/+4, pos +0xc/+0x10/+0x14 (z
0x3f6e), +0x18 live, +0x1a kind (0 wander, 1 player near, 2 collected), +0x1b steps, +0x1c
anim cursor, +0x20 screen, +0x24 tick, +0x25 type 7 steelie / 8 slinky / 9 acid (draw type,
palette sets 5/6/4), +0x26 direction (16ths, 0x10 stuck).

- Band (0x144e4 mode 4): entering 0x1d-0x38 spawns all nine (0x18ffa, not while one is live;
  the silly race start is in the band), leaving clears them (0x190ee).
- 0x18ffa: types 7, 7, 7, 8, 8, 8, 9, 9, 9; position (rng(0x20) * 4 + 0x2ca, rng(0x20) * 4 +
  0x2d2) until 0x1937c is free; kind 0, v 0, 0x194ba, 0x199d6, draw list insert.
- 0x1937c: blocked by another live, not collected mini within 12 px on both axes or a set bit
  of the 16 x 16 cell bitmap 0x24496 (from cell 0x59, 0x5a; outside counts as set).
- 0x1912c: kind 1 for every mini not collected while a player stands on z word 0x3f6e in
  zone 1 (the mini field floor), else 0. Ticker +0x24 (every tick in kind 1, else every 3rd);
  steelie: pos += v, every 4th step 0x194ba (collected: anim steps, list end 0x194ba); the
  others: anim steps, at the list end pos += v and 0x194ba. Always 0x199d6.
- 0x194ba: kind 0: 0x1960e (steelie direction += rng(5) - 2; others rng(2) * 8 (+4 when vx !=
  0), rng(4) == 0 stops an acid), 0x19692 (12 tries, turn 1 or 4); kind 1: 0x1973c (nearest
  player by octant distance below 0x140; steelie away from it in 16ths by axis ratio and
  quadrant, others one axis towards it), 0x198bc (9 tries, the back direction skipped); then
  0x1953e: walk anim steelie 0x21f72, slinky/acid by v (y > 0, y < 0, x > 0, x < 0, still:
  0x219b2/0x21e3e, 0x21a32/0x21eb6, 0x2197a/0x21d4e, 0x219ea/0x21dc6, 0x2194e/0x21f06). Kind 2:
  anim 0x21f8a / 0x21a62 / 0x21efe.
- 0x19976: step = table words 0x244b6/0x244d6 << 8 for direction +0x26; v = step (steelie /
  4). The turn routines keep the position, so only v changes.
- 0x1924e (players in zone 1): |marble - mini| below 4 px (steelie 5) on both axes: kind 2, v
  0, 0x194ba, anim 0x21efe (acid) / 0x21f7a (steelie), race time +3 s (+0x6a), points
  0x285b0(14) and 500 (0x28608).

Checks: `minis` free runs (no RNG input) equal on b1 (702-898: 98 ticks; 904-1499: 296, mini
clear, birds, carry, mini respawn), b2, the tube traces t3/t5/t6 (297/297/373) and
`$TMPDIR/mn` m1/m2 (marble in the field, kind 1 throughout: 346/345), m3 (placed on an acid
mini: collected, 249), m4 (on a steelie mini: it flees, 242). Sweep: silly 2 spawns, 2
clears, longest draw list 19 (minis and birds). Old commands stay exact with minis on.

C++ (`marble_minis.{h,cpp}`, 1:1 port of `marble_mini.py`, internals in namespace
`marble_minis`): `marble_enemies` forwards `init`, `sillyBands` (minis before the bird flag),
`update` (after the acid) and `contact_1924e` (`physics_121b8`, players, before the birds).
Host test `test_marble_minis`: fixture replay (player, minis, draw list in order, RNG), race
sweep (all nine live exactly in the band; silly 2 spawns, 2 clears), a collect at the race
start and a race switch. The race start RNG test expects silly 0xcf09 / 0x400444 = 9 (MAME:
9).

Drawing: `marble_drawlist::build` publishes types 7/8/9 at their list positions as
`marble_mo.other_mos` draws them: origin +0x20 (x) and camera top + 240 - +0x22, culled as the
arcade (-0x10 < sy + 0x10 < 0x100) widened by the remake margin, frame rl(+0x1c) from the death
set with `SET_DEATH | (SET_MINI + type - 7)` (`marble_death_pal` rows 5-7), no masks. Host
tests: `test_mini_objects` (every fixture tick at the arcade cull: columns, palette sets 5/6/4
as MO words equal to MAME; 2183 drawn mini ticks, none dropped by `OBJS`), `test_mini_edges`
(every mini frame at every view edge, guarded tile buffer). Flash after Phase 4b-4b: 4105165 B
(+5368 over the tubes, 23.0 KB left).

Mini fixture `source/test/test_marble_minis/marble_minis_trace.h` (`make_fixture.py
mnwrite`): b1 702 (98 ticks, wander), m3 1000 (120, kind 1, collect), b1 1301 (98, respawn
with birds live), t6 904 (100, tube).

```sh
python3 romconv/internal/marble/tools/make_fixture.py mnwrite \
  source/test/test_marble_minis/marble_minis_trace.h 4:$TMPDIR/bd/b1/trace.bin:702:98 \
  4:$TMPDIR/mn/m3/trace.bin:1000:120 4:$TMPDIR/bd/b1/trace.bin:1301:98 \
  4:$TMPDIR/tu/t6/trace.bin:904:100
python3 romconv/internal/marble/tools/marble_mini.py mnfixture \
  source/test/test_marble_minis/marble_minis_trace.h
python3 romconv/internal/marble/tools/marble_mini.py minis TRACE RACE FROM:TO
```

### Tubes

Marble Love TS (`sub-29cce.ts` runTube12 .. runTube26Or27, `helper-1cd00.ts`). Mode 1 slots,
spawned by camera band (beginner bands 20-47 / 31-64, intermediate 23-50, ultimate 0-12/15).
An entry teleports the marble to a fixed exit (second exit if 0x17cb8 finds the first taken,
radius 0x70), +0x59 0x12, state 3, sounds 0x15884 and 0x35 (0x22: 0x36). Positions are slot
cells; d1/d2 = slot cell - marble cell, d6/a0 = slot px - marble px, z = marble z word.

| tag | race, slot cell, z | acts | exit |
| --- | --- | --- | --- |
| 0x12 | beginner (64, 78), 0x3f88 | hole: d1 -2..2, d2 -1..4, z > 0x3f40; surface 0x12 on d1 0..2, d2 0..3; z >= 0x3f78 rim shape 0; d1 >= 0: z >= 0x3f60 v = 0 (drops), else exit (+0x58 kept; a steelie: 0x15bd0) | x 0x20c (0x204), y 0x264, v (4 px, 0) |
| 0x13, 0x14 | beginner (81, 86), (87, 86), 0x3f30 | d6 -0x18..0x1f, a0 -0x10..0x17: d2 0 and d1 0..2 exit, else mouth walls shape 2 | x 0x29c, y 0x2e4 (0x2dc), v (0, 4 px) |
| 0x15 | beginner (67, 78), 0x3f30 | d6, a0 -0x18..0x27, z < 0x3f40: shape 1 | |
| 0x16 | beginner (84, 94), 0x3f00 | d6 -0x10..0x1f, a0 -0x10..0x17: shape 3 | |
| 0x20 | intermediate (78, 82), 0x3fc0 | as 0x12 with z levels 0x3f80, 0x3fb0, 0x3fa0; no steelie case | x 0x26c, y 0x254 (0x25c), v (0, -4 px) |
| 0x21 | intermediate (78, 75), 0x3f44 | d1 -1..3, d2 -1..2, z < 0x3f60: shape 4 | |
| 0x25 | ultimate (23, 32), 0x4000 | d1, d2 0..1, z < 0x3fe0: exit (sound 0x46) | x 0xf0, y 0x100 / 0x140 by rng(2), v (4 px, 0) |
| 0x26, 0x27 | ultimate (31, 32), (31, 40), 0x4000 | d6, a0 -0x18..0x27: shape 6 | |
| 0x22-0x24 | silly | below | |

A shape that loses the marble (0x1cd00 result 1) leaves 0x29cce without its epilogue. Points
at the end of state 3: 0x12 entry 9, the other entries 5 (2000).

Check: `marble_sim.py deaths TRACE RACE 1340:2200` and `marble_piston.py pistons` (all
enemies) on tick start traces `$TMPDIR/t4c` (`MARBLE_PLACE` at frame 900 next to the tube,
input from 1350 after the camera scrolled there): b1/b4/b6/b8 0x12 (rim, stop, exit, lost
behind 0x15), b2/b5/b7 0x13/0x14 exits and 0x16, b3 a 0x14 mouth wall bounce, i1/i2 0x20 and
0x21, u1/u2 0x25 and 0x26/0x27: all ticks equal. Shape plane hits in these traces: one (shape
2); shapes 0, 1, 3, 4, 6 are only crossed without a hit (same 0x1cd00 code as the silly exits,
12 hits).

#### Silly race tubes


Marble Love TS (`sub-29cce.ts` runTube22, runTube23Or24, runTubeTeleport, `helper-1cd00.ts`,
`refresh-frame-10fce.ts` JT[3]). Three mode 1 slots spawn at the silly race start: 0x22 funnel
at cell (89, 90), z 0x3f74; pipe exits 0x24 (86, 86) and 0x23 (92, 86), z 0x3fb4. Without them
the marble rolling into the funnel stays in its pit (the user's "bridge without top").

- 0x22 (marble cell = slot cell - 1 on both axes): marble z word above 0x3f94: +0x36 0, +0x59
  0x12, v = (0, -4 px), exit x = 0x2dc or 0x2ac by rng(2) (the other if 0x17cb8 finds it
  taken, radius 0x70), y 0x2b0, z = cell floor (0x1bab2 + 0x1cc62(0), also 0x40068c), +0x58
  = 0x22, state 3, sounds 0x15884, 0x36. On the funnel floor (z 0x3f74) z += 2 px; every tick
  in the cell: vz += 0x9000, v x/y 0, airborne: the marble rises out, then drops.
- 0x23/0x24 (|slot - marble| px: x -0x10..0x1f, y -0x18..0x27): 0x1cd00 shape 5 at the slot
  cell corner + 8: plane entries (normal words, box bytes, point bytes; last entry nx > 0x800,
  nx - 0x1000). New or old position in a box and the plane crossed or within 0x400: old
  position deep off the plane: back to the old position, velocity minus its normal part;
  shallow: airborne and fallen over 16 px: lost (+0x57 0x64, 0x25bae(4); objects 0x15bd0)
  and 0x29cce returns without its epilogue; else old position, velocity reversed.
- Player state 3 (0x25514): 0x1b9cc(1), dots, 0x1281c (hidden while +0x58 is set: 0x264aa
  mode 0); +0x59 counts down, then points (tag 0x22: entry 5 = 2000), +0x58 0, +0x22/+0x26
  cleared, state 0 (the exit velocity starts).

Check: `marble_sim.py deaths TRACE 4 FROM:TO rng` (the RNG word an input each tick: the minis,
not ported, use it at the silly start) on tick start traces `$TMPDIR/tu` (place
900:748:716:-140, stick c7 -8 from frame 920): t3 funnel, drop, roll, fall, lost, respawn
(297 ticks equal), t5-t7 back into the exit walls (373 ticks equal, 12 plane hits). Beyond
frame 1650 the race timer warning (+0x71, vblank) differs, not modelled. The 16 px loss path
is not covered by a trace.

Tube fixture `source/test/test_marble_deaths/marble_tubes_trace.h` (`make_fixture.py
twrite`, death fixture format plus `rng_in`, all enemies run; a `.h` spec keeps that file's
segments): silly t3 904 (296 ticks), t6 904 (372 ticks); b1 1440 (110), b2 1429 (40), b3
1429 (30), b5 1469 (50), i1 1416 (50), u1 1375 (50). C++ (`marble_objects.cpp` `funnel_22`,
`tubeHole`, `tubeEntry`, `tubeHoleU`, `tubeWall`, `shape_1cd00`; `marble_physics` state
`TUBE`, `blocked`): `test_replay_tubes` replays it, `test_silly_funnel` and `test_tube_runs`
(0x12, 0x13, 0x20, 0x25) roll the marble into a tube through the machine start path. Flash
+1800 B (silly), +1160 B (other tubes).

```sh
T=$TMPDIR/t4c; H=source/test/test_marble_deaths/marble_tubes_trace.h; cp $H $T/old.h
python3 romconv/internal/marble/tools/make_fixture.py twrite $H $T/old.h \
  1:$T/b1/trace.bin:1440:110 1:$T/b2/trace.bin:1429:40 1:$T/b3/trace.bin:1429:30 \
  1:$T/b5/trace.bin:1469:50 2:$T/i1/trace.bin:1416:50 5:$T/u1/trace.bin:1375:50
python3 romconv/internal/marble/tools/marble_sim.py tfixture \
  source/test/test_marble_deaths/marble_tubes_trace.h
```

### Pistons in the main tick model (Phase 4b-5)

Marble Love TS (`post-state-change-13966.ts`, `state-sub-186ac.ts`, `read-abs-long-1844a.ts`,
`state-sub-1881c.ts`, `sprite-coords.ts` 0x18972, `sprite-derive.ts` 0x1bb28,
`object-render-update-1365c.ts`), plus three facts from the disassembly: 0x121b8 refreshes the
cell (0x1bab2) when 0x1881c returns 1 (0x123f8); 0x1365c clears / recloses aerial slots on a
zone 4 entry (0x13892, see Marble physics 7); the launch sign quirk below. Model
`tools/marble_piston.py`, `marble_piston.enable(m)` (`m.pistons`, on in `marble_enemy.setup`).

Entry 0x401650 + 0x10 k: +0 k, +2 timer word (0xffff up), +4/+5 cell x/y, +6 top z word, +8
anim cursor, +0xc screen (x, y words). 0x400760 armed, 0x400764 timer table of entries
0x18-0x23. Cells: (69-71, 55-58) top 0x3fac, (69-71, 63-66) and (76-78, 74-77) top 0x3f98.

- Zone hook 0x13966 (from 0x1365c on a zone change and from the respawn): silly sound latches,
  aerial latches and 0x400768, 0x186ac, zone wait slots of the new zone restarted.
- 0x186ac (race 3): a live player in zone 4/5 and 0x400760 clear arms: variant rng(4),
  0x400764 = long 0x243e6[v], entry k timer word 0x242c6[k] (k < 0x18) or word k - 0x18 of
  the variant table, cell bytes 0x24406/0x2442a, 0x1bb28 (cell * 8 into 0x400690/2 and
  0x1bb50: leaves the last cell in the scratch the rest of the player's 0x121b8 reads), top
  0x2444e; then 0x400764 = long 0x243f6[v], 0x400760 = 1. No such player and armed: pistons up
  leave the draw list, timer/cell/top 0, 0x400760 = 0.
- 0x1844a (race 3, armed; after the birds): timer >= 0 counts down, at <= 0 the piston rises
  (+8 = 0x21342, timer 0xffff, draw list insert type 0x29); up: +8 += 4, at the list end
  (13 frames) it drops (draw list removal, timer reload 0x242f6[k] or the variant table).
  0x18972 for every piston up. One rng(2) per group (0-0xb, 0xc-0x17, 0x18-0x23) that rose
  (sound pick).
- 0x1881c (race 3, armed, players, after the acid): a piston up on the new cell (0x400697/9):
  position back to 0x400684/8; the old cell the piston's and z word = top: launch (vz 0x70000,
  z + 12 px, v = v / 2 +- 0x6000, +0x36 = 2, sound 0x45); else z - top < 12 px: v reversed.
  The sign: 0x13a98 writes only the low word of d0 and 0x1881c tests the long, so the first
  draw is always plus (d0 high word 0xffff from the loop), the second plus unless the new vx
  high word and the draw are both 0.
- Not modelled: sounds, 0x400768 countdown (0x117b2: sound 0x13, 0x40076a bit 2).

Traces (`$TMPDIR/ps`, `$TMPDIR/st/run.sh`, race 3, place 900:520:428:-84:4, input
`60:2:-2` from 950):

| run | second place (frame 1361) / input from 1440 | content | free run |
| --- | --- | --- | --- |
| p1 | none | arming 1039 (zone 6 to 4), respawns, rise/drop | 348 ticks equal |
| p3 | 564:452:-84:5 | marble on piston 4, launch 1585 | 167 ticks equal |
| p4 | 564:452:-84:5, `40:4:-4` | launch 1493, bounce 1782 | 217 ticks equal |
| p6 | 620:660:-120:5, `40:4:-4` | teardown 1490 with 10 up (zone 5 to 7) | 217 ticks equal |

Piston fixture `source/test/test_marble_pistons/marble_pistons_trace.h` (`make_fixture.py
pswrite`): p1 1001 (40 ticks, arming), p1 1241 (10, teeter at a piston pit), p3 1575 (24, launch), p4 1483 (20, launch), p4 1770
(12, bounce), p6 1478 (16, teardown).

C++ (`marble_pistons.{h,cpp}`, 1:1 port of `marble_piston.py`, internals in namespace
`marble_pistons`): `marble_enemies` forwards `arm_186ac`, `contact_1881c` and runs 0x1844a
after the birds. `marble_physics` gains the zone hook 0x13966 (`zoneHook_13966`, from
`zones_1365c` on a zone change and from the respawn), the 0x1881c call with the cell refresh,
the teeter fix and `cellScratch` (0x1bb28); `marble_objects` the slot parts `zoneScripts`
(0x13966) and `zone4_13892`. A piston's draw list index sits at +0 (the list reads +0x19 of
the object, so the port passes entry - 0x19). Host test `test_marble_pistons`: fixture replay
(player, cell scratch, pistons, draw list in order, RNG), race sweep (never armed without a
zone change), the arming scratch, launch / bounce / wall / down cases of 0x1881c, teardown
and a race switch with pistons up.

Drawing: `marble_drawlist::build` publishes type 0x29 at its list position as
`marble_mo.other_mos` draws it: origin +0xc (x) and camera top + 240 - +0xe, culled as the
arcade (-0x40 < sy + 0x10 < 0x100) widened by the remake margin, frame rl(+8) from the death
set with `SET_DEATH | SET_PISTON`, no masks. Host tests: `test_piston_objects` (every fixture
tick at the arcade cull: columns and palette set 4 as MO words equal to MAME; 478 drawn piston
ticks, none dropped by `OBJS`), `test_armed_sweep` (armed aerial sweep: at most 16 pistons up,
15 objects), `test_piston_edges` (every piston frame at every view edge, guarded tile buffer).
Flash after Phase 4b-5: 4107841 B (+2676 over the minis build, 20.4 KB left).

```sh
S=$TMPDIR/ps
python3 romconv/internal/marble/tools/make_fixture.py pswrite \
  source/test/test_marble_pistons/marble_pistons_trace.h 3:$S/p1/trace.bin:1001:40 \
  3:$S/p1/trace.bin:1241:10 3:$S/p3/trace.bin:1575:24 3:$S/p4/trace.bin:1483:20 3:$S/p4/trace.bin:1770:12 \
  3:$S/p6/trace.bin:1478:16
python3 romconv/internal/marble/tools/marble_piston.py psfixture \
  source/test/test_marble_pistons/marble_pistons_trace.h
python3 romconv/internal/marble/tools/marble_piston.py pistons TRACE 3 FROM:TO
```

### Aerial objects (Phase 4c-2)

Marble Love TS (`sub-29cce.ts` runCatapult0A, runGate0B/0D, runBounce0C,
`refresh-frame-10fce.ts` JT[10], `slot-spawn-pattern-13d38.ts`); model
`tools/marble_aerial.py` (`enable`, on in `marble_enemy.setup`). Aerial slots (objects table
above): six gates at bands 3-30, each waiting (state 4) for its zone 30-35 (zone lines at the
corridors, e.g. cells (31..35, 36) down: 30, x 38 right: 31, x 47 right: 32); catapult 0x0a at
cell (63, 70), bands 16-37; four bounce boxes 0x0c (random group 0x1ded8, x cell 105, y
102-108) waiting for zone 10 (x 99 right or y 99 down).

- 0x0b / 0x0d gate (0x2ab88 / 0x2ad20): only while it sucks (slot +0x1a != 0, +0x46
  0x22016 / 0x220a6). Mouth (0x0b: d6 -0x0c..0x1b, a0 -0x20..-9; 0x0d: d6 -0x20..-9, a0
  -0x0c..0x1b): v += 4 x the unit vector (0x29cce distance measure, divs) to the centre (0x0b:
  d6 - 8, a0 + 0xc; 0x0d: d6 + 0xc, a0 - 8); in the centre (0x0b: d6 0..0xf, a0 -0xf..-0xa;
  0x0d transposed) caught: state 0xa, +0x57 0x20, +0x58 the slot number. Else the frame
  (0x0b: d6 -0x0c..0x1b, a0 -8..0xf; 0x0d transposed, ROM 0x2ae1a; Marble Love has the 0x0b
  box for both; MAME a9 bounces on the 0x0d frame) below z 0x3fc4: inside it before the move
  (0x0b box for both gates, 0x2ae42): lost (+0x57 0x65, 0x25bae(4)); else both axes bounce.
  The 0x0d path reads the old deltas from frame locals -0x12/-0x14 that only the 0x0b and
  0x0c paths write (stale or stack garbage in the arcade); the model computes them.
- State 0xa (0x2563e): projection, 0x13d38 stars (8, code 0x10b + k, +0xa4 first 4, +0x38
  next 4: from the marble's screen position to the gate mouth to the gate, one star step per
  2 ticks, offsets 0x1ef32), 32 ticks; then the gate script restarts (0x1d752 / 0x1d798,
  sound 0x1ef5a[zone - 0x1e]), sound 0x3c, +0x5a 0, death count + 1, state 4 directly (no
  0x25bae: the marble respawns where it was caught), +0x57 0x65.
- 0x0a catapult (0x29e22): |d6|, |a0| < 8 and vz 0: arm busy (+0x3e != +0x46 or +0x1c):
  old position; else thrown: slot position, z - 3 px, vz 10 px, vx rng(0x2000) - 0x1000, vy
  -0x25000 - rng(0xa000), airborne, sound 0x15884, state 3 (+0x58 0x0a, +0x59 15), slot script
  0x1db80 run at once. Two players (0x400396 == 2, partner on the arm): not modelled.
- 0x0c bounce box (0x2a9a2, slot state != 0): box of the slot's MO frame (rl(+0x3e): bytes x,
  y, w, h at +4) widened by 3 px; entering: both axes bounce; already inside: v = 64 x the
  unit vector away from the centre (d6 + 4, a0 + 4), dizzy (anim 0x20faa, sound 0x39 unless
  in state 1), 0x25c74 with fall 0, +0x57 0x3c, +0x56 0.
- rng (0x13a98) reduces with a signed compare: rng(0xa000) returns 0x8000..0x9fff (the model
  and C++ compared unsigned before; only the catapult passes n >= 0x8000).

Check: `marble_aerial.py aerial TRACE 3 FROM:TO` (all enemies, `marble_piston.piston_diff`)
on tick start traces `$TMPDIR/t4c` (run.sh as section "Tubes"): a5 (place 268, 290, -84,
input 1350 "40:1:-7,60:7:2") 0x0b catch, state 0xa, respawn; a8 (300, 308, "150:9:0") pull,
catch at the second gate; a9 (372, 308, "30:9:0,100:2:-6") 0x0d frame bounce, fall, lost;
a10 (484, 560, -108, "30:9:0") catapult throw, landing, dizzy (equal to frame 1627; then a
steelie on the aerial course differs, also without aerial objects: open); a13 (place 786,
804, -156 and 1640: 840, 800; "60:12:0,250:0:0,60:2:-6") zone 10, two bounce box hits, dizzy,
lost. All other ticks equal. Not seen: 0x0d catch, gate frame loss.

C++: `marble_objects.cpp` (`catapult`, `gate`, `gateFrame`, `bounceBox`, `closeGate`),
`marble_physics.cpp` (state `CAUGHT`: `caught_2563e`, `suck_13d38`; `land` public for the
bounce box), `marble_drawlist.cpp` (stars in `CAUGHT`). Fixture
`source/test/test_marble_aerial/marble_aerial_trace.h` (`make_fixture.py awrite`, all enemies,
replay `marble_sim.py afixture`): a5, a8, a9 1341 (140 ticks each), a10 1341 (139), a13 1643
(210). `test_replay_aerial` replays it, `test_gate_catch` and `test_catapult` run the a5 and
a10 moves through the machine start path. Flash +2428 B.

```sh
T=$TMPDIR/t4c
python3 romconv/internal/marble/tools/make_fixture.py awrite \
  source/test/test_marble_aerial/marble_aerial_trace.h 3:$T/a5/trace.bin:1341:140 \
  3:$T/a8/trace.bin:1341:140 3:$T/a9/trace.bin:1341:140 3:$T/a10/trace.bin:1341:139 \
  3:$T/a13/trace.bin:1643:210
```

## Game flow

Phase 3a (plan docs/superpowers/plans/2026-10-12-marble-flow.md). Research: Marble Love TS
game-tick-timers.ts, timer-cascade.ts, object-scan-dispatch-251de.ts,
main-loop-init-1101e.ts, player-slot-iter-118d2.ts, main-loop-init-10504.ts,
level-intro-banner-resume.ts (fitted to MAME captures, not a port), state-sub-18a88.ts,
object-charcode-broadcast-1bbaa.ts, refresh-frame-10fce.ts JT[8]. State 8 entry: disassembly
0x256d2-0x25730 (exception, not in TS).

- Race timer (vblank IRQ 0x28a96, every 60 Hz frame): player +0x6a word seconds, +0x6c tenths
  (9..0), +0x6e frames (5..0, 0xff stopped), cascade 0x28c38; 60 s = 3660 frames from the
  race start. Skipped while +0x1a = 8. Seconds below 0: +0x18 = 2, timer 0 and stopped,
  +0x71 = 0xff, 0x400390 = 4.
- 0x400390 (0x1101e): 0 race, 1 attract, 2 game over / high score, 3 race end, 4 time out,
  5 game start, 6 final. 0x251de tail: all players +0x18 = 3 (goal countdown) and not
  attract: 3.
- Race end (frames after the 0x400390 = 3 frame, MAME traces g0-g5, +-1): race index + 1 at
  2; bonus min(seconds, 99) * 100 at 2 + 40 + level load (81, 80, 60, 116, 73 frames by race
  finished; ultimate 0); next race init 8 (races 0-2) / 9 later frames after the bonus; the
  intro adds 5 s every 10 frames from 64/63/63/64/64 frames after the init: races 0/1 set
  60, later races add 35, 30, 20, 20 to the time left; the race starts (0x400390 = 0, +0x6e
  = 5) 61 + race time frames after the last arm (first add - 1). New game: first add 131
  frames after the init (60 frame scroll in). 0x400010 at the race start = at the race end
  + 6. RNG word: carried, frozen during the race end.
- Next race carry (`marble_flow.py carry_check`): cleared work RAM with the RNG word, race
  start (begin_race), player +0x6a-+0x6f, +0xbc score, +0xd2 deaths, 0x400010 equals the MAME
  RAM before the race start for 400 idle ticks (player fields, RNG, slots) in all 5 next
  races. Leftover RAM of the previous race is not read.
- Final (after ultimate): 0x400390 = 6 6 frames after the bonus; 20000 + 1000 * min(s, 99) -
  1000 * min(deaths, 20) added 250 per 2 frames from 10 frames on; game over 272 frames
  after the last step. Time out: game over 182 frames after 0x400390 = 4.
- Time bonus (easter egg): 0x1bbaa (armed at the race start, rng byte 0x400444 below
  0x24a94[race] = 0, 200, 175, 150, 125, 100; player rolling in a zone of 0x24aae[race] with
  3-6 s left) sets +0xcb; state 0 enters state 8 (+0x56 = 9, +0x57 = 10, +0xcc = 0x216aa,
  sound 0x53, +0xd1 = 1); 0x258a8: +1 s every 9 ticks, 10 times, timer paused, then rolling
  and 0x285b0(0x10).

Validation: `marble_flow.py check` on frame traces ($TMPDIR/t3a, `mame_trace.lua
MARBLE_TICK=frame`): practice time out p0 (3660 timer frames), goals g0-g5 (MARBLE_PLACE on
the goal line at 900 and 1350 with MARBLE_PLACE_V, MARBLE_RACE_UNTIL=1000), time bonus s8
(MARBLE_POKE 5 s, armed, rng byte 0): timer equal every race frame, race end sequences within
+-1 frame, values exact, carry 400 ticks equal. State 8: `marble_sim.py deaths` on tick trace
s8t (173 ticks equal, player MOs too).

Remake: `marble_flow.{h,cpp}` (sequencer, timer, carry), `marble_physics.cpp` (goal points,
timer stop, 0x1bbaa, state 8, 0x251de tail, score wobble 0x25416), `marble.cpp`
(`run_frame` calls `marble_flow::frame` every frame; logic ticks only in LOGIC; `loadRace`).
Game over: START (EXTRA) or FIRE starts a new game at practice, else the title follows
(section Attract). Host test
`test_marble_flow`: time out from a new game, race ends (MAME offsets +-1, values exact),
carry runs (crc of the model run), time bonus segment (`marble_bonus_trace.h`).

```sh
T=$TMPDIR/t3a
python3 romconv/internal/marble/tools/marble_flow.py check $T/p0/trace.bin $T/g?/trace.bin
python3 romconv/internal/marble/tools/make_fixture.py fwrite \
  source/test/test_marble_flow/marble_flow_trace.h $T/p0/trace.bin $T/g?/trace.bin
python3 romconv/internal/marble/tools/make_fixture.py tbwrite \
  source/test/test_marble_flow/marble_bonus_trace.h 1:$T/s8t/trace.bin:1351:120
```

## HUD

Phase 3b (plan docs/superpowers/plans/2026-10-12-marble-hud.md). Alpha layer (atarisy1.cpp):
RAM 0xa03000, 64 x 32 words, word bits 0-9 code, 10-12 colour, 13 opaque (pen 0 drawn, else
transparent); font `136032.104.f5` (atarisy1.zip, 512 glyphs, 2bpp, `anlayout`); palette entries
0-31 (colour * 4 + pen), ROM 0x1FBD0. Visible columns 3-38 (column 39 holds the last column of
the big final score). Codes 0x00-0x7f small font, ASCII (`' '` = 0); 0x100-0x17f big font:
digit d = 2x2 tiles 0x100 + 4d, space digit wide (0x13c), comma one column (0x13c over 0x16e),
big letters proportional (tile runs captured from MAME, the BIOS tables 0x72ac/0x0ccc are not
read). Texts: ROM strings (`tools/marble_hud.py` S_*), number formats from the captures.

Items (arcade row, column, colour; o = opaque):
- Top (race init + 4 until the bonus / time out end): `_____` row 0 col 3 (7), `SCORE` row 1
  col 3 (0o), score row 2 col 3 with commas (0o); timer rows 1-2 cols 19-22, clamp(seconds,
  0..99) two big digits with leading zero (2o; 0x286EE clamps at 99), border 0x5f row 0,
  0x5e row 3, 0xdf col 23, corners 0xff / 0x1b (7).
- Race intro (first +5 s - 60 frames until the race starts): rows 8-11 cols 8-33 (4o): blank
  row, `TIME TO FINISH` (races 0/1) or `EXTRA TIME FOR`, the race line (race name 5o, rest 4o),
  blank row; seconds still to add, two big digits cols 29-32 (5o, leading space). Silly race:
  rows 14-16 cols 6-36 (5o) `EVERYTHING YOU KNOW IS WRONG` between blank rows.
- Race end (0x400390 = 3, from frame 3 until the bonus frame, where 0x118d2 sets +0x70 = 0xff
  with the points, also with 0 s left): rows 3-4 col 3 (0o) `BONUS FOR`,
  `TIME LEFT:` + min(s, 99) * 100 right aligned at col 19; drawn after the top items (covers the
  timer border cell at col 19). Then the alpha palette is off until the next race init + 4.
- Time out (0x400390 = 4 from frame 2 until 2): rows 24-28 cols 13-28 (0o) `OUT OF TIME`,
  `GAME OVER` between blank rows; top items until 2 + 2, then the attract mode (Phase 6).
- Final (0x400390 = 6 from frame 11 until game over + 3; palette on from frame 2, playfield
  black, bouncing marble MOs not modelled): big `CONGRATULATIONS!` rows 2-3 col 6 (4),
  `YOU HAVE COMPLETED` / `THE ULTIMATE RACE!` rows 7-8 col 12 (5), rows 13-19 bonus lines
  (labels 5, seconds and deaths 4, values right aligned at col 38; TOTAL = final bonus left),
  big `FINAL` rows 22-23 col 4, `SCORE:` rows 24-25 col 12, score right aligned at col 39 (4).

Palette (32 IRGB words): ROM 0x1FBD0 from the race init + 4 (with the intro banner at the
latest), all 0 during the level loads. Race end 0x118d2: entries 0, 4, 8, 12 = 0 (boxes black),
9, 13 = 0xafff, 29 = 0 until the bonus frame. Pulses (vblank, before the race timer; table
0x20b34 = 0x20b74, 16 words f00f..ffff..f00f): entry 3 = table[(+0x70 - 1) >> 2] while +0x70 !=
0xff and +0xd8 = 0 (score fade: 0x118d2 sets +0x70 = 0), entry 11 = table[(+0x71 - 1) >> 1]
(timer flash: 0x28a96 sets +0x71 = 0 when a second passes to 4 s, 0xff and entry 11 = f00f
above 5 s, at the time out and in 0x118d2). Entries stay until written again (sticky).

Validation: `marble_hud.py check` on frame traces with `MARBLE_ALPHA=1` (practice time out
p0, goals g0-g5 as in "Game flow", g5 placed at 1350): HUD state from work RAM (`hud_states`),
alpha cells (visible content) and palette equal to MAME in every frame within +-1 frame
(7 runs, 17407 frames).

Remake layout (30 x 36 cells, `view`): top items 3 columns left (timer 6, centred), banners and
the final screen 6 columns left and 3 rows down (arcade screen centred in the 288 lines); final
labels 7, values 9 columns left; cells outside are clipped (the `!` of `CONGRATULATIONS!`, the
last blank of the silly banner). Game over leads to the title (section Attract).

Remake: `marble_flow` steps the pulse counters every frame before the race timer (0x26bee,
0x26c78), the timer flash on/off (0x28a96 at 4 s / above 5 s) and the 0x118d2 counter writes
(+0x70 = 0, +0x71 = 0xff, +0xd8 = 0 with the race index; +0x70 = 0xff with the bonus points);
the race start sets +0x70 = +0x71 = 0xff (0x259b4). `marble_hud::update` derives the HUD state
of each frame from the work RAM image and the flow counters as `hud_states` (emulation core,
published in VideoState); `build` / `blend` draw it on the video core (grid of alpha words
rebuilt on change, blended over each strip after the MOs; the final screen skips playfield and
objects). Host tests: `test_marble_hud` (font, palette, layout cases of `marble_hud.py`
LAYOUT_CASES against `view`, blend), `test_marble_flow` (HUD state every frame of the time out
run and of all race ends equal to `hud_states` of the MAME traces, +-1 frame;
`marble_hud_trace.h`, `make_fixture.py hwrite`).

```sh
T=$TMPDIR/hud/a   # tools/mame_trace.lua MARBLE_TICK=frame MARBLE_ALPHA=1, runs p0 g0-g5
python3 romconv/internal/marble/tools/marble_hud.py check $T/p0 $T/g?
python3 romconv/internal/marble/tools/make_fixture.py hwrite \
  source/test/test_marble_flow/marble_hud_trace.h $T/p0 $T/g?
```

## Attract

Phase 6a (plan docs/superpowers/plans/2026-10-13-marble-title.md). Research: Marble Love TS
particle-init-18cd2.ts, particle-bounce.ts, palette-rng-fill-26cfa.ts,
high-score-defaults.ts, hi-score-decode-41c8.ts, late-game-logic-26f3e.ts (type 0x2c),
mode2-init-11452-async.ts (segments); timing, layout, logo map and logo palette fitted to MAME
frame traces (`mame_trace.lua MARBLE_ATTRACT=1`).

- Sequencer (0x400390 = 1, segment byte 0x4003e4, demo mode 0x400394 alternating 0/1): boot
  runs 305 frames, then each segment is a demo race (6b), then the title. Title: particles
  (0x4003e2 != 0) for 600 frames, alpha palette off 1 frame later, cells cleared 2 later,
  next segment 6 frames after the title end; title starts 6 frames after the demo's alpha
  clear (1332 -> 1338). Game over without high score entry (0x400390 = 2 at G, one
  rng(0x100) at G): 0x400390 = 1 segment 0 at G + 7, title at G + 14. Game over with entry:
  6c (entry particles 6, mode 0; the title after the entry runs 299 frames).
- Title cells (alpha, transparent): `TROUBLEMAKERS` row 9 col 15 (colour 5, ROM 0x235a8); table
  rows 11-20 from col 13 (colour 4): rank right aligned in 3 (`#1`), space, 3 initials, score
  with commas right aligned in 9; `@ 1984 ATARI GAMES` row 29 col 12 (4, 0x22f04); `1 COIN PER
  PLAY` row 28 col 3 (5, 0x22f6c), `CREDITS:  0` + 4 spaces row 28 col 24 (5, 0x22ff8). Drawn
  over 3 frames: header and table (last cell one frame late), then the copyright, then the
  coin line. Alpha palette ROM 0x1FBD0. Remake: dx -6, dy 3 (coin line dx -2, credits -7).
- Table: RAM 0x401e74 + 0x1e, 10 x 5 B (24 bit score, initials radix 40 word: space, A-Z,
  0-9, `:;<`); cold boot defaults ROM 0x1eea0 (10 x 8 B: long score, 3 ASCII initials):
  C R 14,500 down to DAR 10,000 by 500.
- Logo: playfield RAM rows 2-7, columns 11-30 (= screen columns 8-27), words 0x0f84-0x0fd0
  (`marble_attract.LOGO`: border rows, three text rows of consecutive tiles), colour set 0.
  Playfield set 0 = palette set 0 with 13 words from ROM 0x20aca at pens 2, 4, ..., 14, 18,
  ..., 28. Remake: map columns 8-27, rows 5-10 (view sy 0).
- Particles (0x400a9c, 10 B: x, y in 1/16 px, vx, vy, mode word; count 0x4003e2): init
  0x18cd2(count, mode): per particle x = (rng(256) + 0x24) << 4, y = (rng(128) + 0x30) << 4,
  vx/vy = rng(96) - 0x30 then +-0x10 away from 0, mode < 0x80: mode << 11, 0xff: rng(8) << 11,
  else rng(2) << 11. Mode 0xff first runs the palette fill 0x26cfa: MO colour set k pens 1-5 =
  0xafff, 0xcfc0 and 3 words of variant rng(2) from ROM 0x20bb4 + 12k (+6). Bounce 0x18dca
  every frame from the frame after the init: move, reflect at x 8..0x148 / y 0..0xe0 px,
  commit only inside. Title: 3 particles, mode 0x80 (blue / red), MO colours of palette set
  0. Final screen: 28 particles mode 0xff, init 3 frames after 0x400390 = 6 from that
  frame's RNG (fill, then init), until game over.
- Particle MOs (0x26f3e type 0x2c): two 8 x 16 halves, codes mode | 1 and mode | 3, MO x =
  px - 8 and px, MO y = py + 16 (top line 224 - py); written into the bank not shown, so the
  screen shows the previous frame. Remake view: x = MO x - 48, y = 248 - py.

Remake: `marble_flow` ATTRACT runs title segments (lead 7, title 600, tail 6), each followed
by a demo race (section Demo race): boot and game over + 7 enter it, START starts practice; game over consumes one
rng(0x100); FINAL inits the 28 marbles at frame 3, game over clears them. `marble_particles`
(init, bounce, view positions in a VideoState union with the objects), `marble_hiscore`
(table, ROM defaults once), the title playfield as level `marble_level::TITLE`, `marble_hud`
TITLE (table read on the video core; version byte in Hud). Host tests: `test_marble_attract`
(data, MAME particle runs, clip, defaults), `test_marble_flow` (boot, game over and final to
the title, START, cycles), `test_marble_hud` (title layout cases, table rebuild).

Validation: `marble_attract.py check` on frame traces a0 (boot attract, 9000 frames), p0
(practice time out to the title), g5 (ultimate goal, final screen, entry, title): particle slots
every frame exact, MOs, palette fill, title cells (+-1 frame) and palettes, game over RNG,
sequencer counts. `marble_attract.py logo` on attract shots: logo words and palette.

```sh
T=$TMPDIR/attr   # run.sh: mame_trace.lua MARBLE_TICK=frame MARBLE_ALPHA=1 (MARBLE_ATTRACT=1 for a0)
python3 romconv/internal/marble/tools/marble_attract.py check $T/p0 $T/g5 $T/a0
python3 romconv/internal/marble/tools/marble_attract.py logo $TMPDIR/attract
```

### Demo race

Phase 6b (plan docs/superpowers/plans/2026-10-13-marble-demo.md). Research: Marble Love TS
waypoint-list-step-1815a.ts, object-step-17f66.ts, refresh-frame-10fce.ts (goal state 6),
state-machine-schedule.ts and string-render.ts (banners); timing fitted to MAME frame traces.

- Each segment after the title (0x4003e4 + 1) runs a demo race, practice and beginner in turn
  (boot: practice first; after game over the title comes first, then practice). 0x400390
  stays 1. From the segment change: level load (black) 59 / 84 frames to the race init (slot
  1, 0x400010 = 1), first logic tick and timer start (60 s, ticks 5) 3 / 4 frames later, then
  the race runs (score, timer, enemies) until the waypoint list ends (0x40075a = 1, timer
  stopped by +0x6e = 0xff: practice tick 477, beginner 378; a MAME tick can span the list
  end frame and the next), title marbles 11 frames later.
  0x400010 counts the ticks: 3 in the first. Paths do not depend on the RNG.
- 0x17f66 (after the slot 2/3 skip) and goal 0x254d2 (before 0x121b8) call 0x1815a while
  0x400390 == 1 instead of the trackball. Cursor 0x400446 = long 0x1d364 + race * 4 (0x2419e
  practice, 20 records; 0x241f2 beginner, 12), record (x, y, speed, banner) bytes, 0 ends:
  target (x << 19) + 0x40000 in 16.16; within 32 units of +0x0c/+0x10 the record is passed
  (several per call), else the velocity eases towards the target scaled by speed << 8 (0xc0
  near), +0x08 gravity when airborne, speed cap 0x26196.
- Banner byte >= 0: BIOS 0x12a(0x5a, 0x3400, long 0x242aa + 4 b) draws the alpha string chain
  (entry: col, row, string long, marker byte (2 = next), next long) in colour 5 opaque and
  erases it 90 frames later (message slot 0: chain 0x401f04, state 0x401f1c). Practice 0
  `FINISH RACE IN THIS TIME`, 1 `WARNING: CLIFFS!`, 2 `AVOID DELAYS`, 3 `BONUS FOR REMAINING
  TIME`; beginner 0, 4, 5, 6.
- MAME ticks take 1-3 frames in the beginner demo (resync at 2 frames average): remake ticks
  every 2 frames, banners +-1 frame.

Remake: `marble_flow` Demo parts after each title (`Flow.demo`, races alternate from practice
by `Flow.demos`): DEMO_LOAD (HUD `BLACK`, 59 / 84 frames), the race load with 0x400390 = 1
(`raceLoaded`: no carry, 0x400010 = 2, cursor `marble_physics::demoStart`, no sound),
DEMO_INTRO (3 / 4 frames, then 60 s and the first tick), DEMO_RUN (logic ticks every 2
frames, BIOS message slot erase after 90 frames) until `demoEnded`, DEMO_END (HUD `DEMO`
4 more frames), then the title at its frame 1. `marble_physics` 0x1815a in 0x17f66 and
0x254d2 while 0x400390 == 1. `marble_hud` DEMO: race top items, coin box (36 arcade columns
fitted into 30), banner 6 columns left kept inside the view; ROM colours. `marble_sound::send`
drops everything in attract. Host tests `test_marble_flow`: `test_demo_races` (title,
practice, title, beginner, title, practice demo; per tick player, cursor, RNG and key CRC
equal MAME, banners 90 frames, title 11 frames after the list end), `test_demo_start_clean`,
`test_demo_gate`, `test_demo_silent`; `test_marble_hud` demo layout cases.

Validation: `marble_demo.py check` (timeline per segment exact; the remake's demo race start,
cleared RAM + race start + cursor, replayed with the traced camera: per tick player fields,
cursor, list end, RNG equal to MAME in all 7 demos of a0 and p0; banners +-1 frame).
`marble_demo.py fwrite` writes `test_marble_flow/marble_demo_trace.h`.

```sh
python3 romconv/internal/marble/tools/marble_demo.py check $T/a0 $T/p0
```

## Sound

Phase 5 (plan docs/superpowers/plans/2026-10-13-marble-sound.md). Arcade: a 6502 at 1.79 MHz
runs the sound program (32 KB, 136033.421/422) and drives a YM2151 (music and almost all
effects) and a POKEY (a few effects). The 68010 sends one command byte per event through the
latch 0xfe0001 (0x158ac, nothing while the word 0x4003b8 is set). The remake emulates none of
this: music, jingles and effects are songs of the C64 tune's own player.

Commands (MAME, `tools/mame_sound.lua`; practice race, music stopped with 0x00 before each):

- 0x00 stops the music, 0x02 does not; 0x03 every frame and 0x07 every 16 frames are silent.
  Attract mode sends no sound.
- New game (0x400390 = 5): 0x02, 0x00, 0x62, then the race music: 0x08 practice, 0x0a
  beginner, 0x0c intermediate, 0x0e aerial, 0x14 silly, 0x18 ultimate (about 60 frames after
  0x62). Goal: 0x3a, 0x3b, 0x2f (goal sound) at the crossing; race end (0x400390 = 3): 0x3a,
  0x3b, 41 frames later 0x18 (practice) / 0x17 (ultimate), then the next race music (roles
  of 0x17/0x18 at the race end open). Time out (= 4): 0x09, 0x3f (time out jingle: a 6 note
  chord and a rising arpeggio, about 290 frames), 0x3a, 0x3b. Game over (= 2): 0x61.
- Multi-channel pieces with open roles: 0x10, 0x16, 0x1a-0x1e. YM effects (mostly channels
  5-7): 0x1f-0x39, 0x3c-0x3e, 0x41-0x43, 0x45, 0x47-0x5c. POKEY effects: 0x5d, 0x5e (acid),
  0x5f, 0x60. No output: 0x09, 0x0b, 0x0d, 0x0f, 0x11-0x13, 0x15, 0x17, 0x19, 0x3a, 0x3b,
  0x40, 0x44, 0x46, 0x48, 0x4a, 0x4c, 0x4e, 0x50, 0x52, 0x61-0x7f.
- Gate tables: ROM 0x1ef5a = 0x48, 0x4a, 0x4c, 0x4e, 0x50, 0x52, 0x2f, 0x30; 0x1ef42 = 0x47,
  0x49, 0x4b, 0x4d, 0x4f, 0x51 (aerial gates, key-on / key-off pairs on music channels).
- Goal lines (zone 0xff, list per race at 0x1eb16, 6 byte entries x, y, end, zone a, zone b,
  cells of 8 px, x < 0: horizontal line y from |x| to end): practice y 63, x 72-79 (also y 68
  and x 71); beginner y 108, x 110-113; intermediate y 100, x 93-94; aerial y 103, x
  113-116; silly y 17, x 19-22; ultimate x 87, y 88-90.

C64 tune: HVSC `GAMES/M-R/Marble_Madness.sid` (romszip, 12798 B, PSID v2, NTSC, VBI, load
0x2700 in the payload, init 0x3e00, play 0x3e40, payload to 0x587f, 6 songs, HVSC lengths
0:16 0:56 1:20 0:55 0:21 0:52). No published source of its player exists (Will Harvey,
Sandcastle); disassembled here:

- Song table 0x3dc0 (word per song, 32 entries). init(A = song) clears the SID, resets the
  slots (0x3081), registers the descriptor (0x3090) and starts slot 0 (0x316b).
- Descriptor: 4 byte id, tempo lo, hi, volume 0x7f, track count, voice mask (bit per SID
  voice: the music uses 0x06, voices 1-2), priority (0x80), track pointers, track lengths.
- Track events, 2 bytes: note `nn dd` (MIDI note number, 0x3c = C4 = 261.6 Hz; dd & 0x3f
  indexes the duration table 0x29f8; bit 7: the next event at once; bit 6: the gate goes on
  through the next event of the same note), `80 dd` rest, `81 i` instrument, `84 v` volume,
  `87`/`88` transpose add/set, `89`/`8a`/`8e` tempo, `8b n` / `8c` repeat, `8d`/`8f` volume
  scale, `ff` end. The gate is the summed duration - 1 frame.
- Timing: every frame the current duration value is added to a phase, an event fires when
  it reaches 0x3f48000 / tempo. Tempo 0x3840: duration index k (0-7) = 2^(7-k) frames.
- Slots (s = 0, 1): count 0x2728, virtual voices in use 0x2727, tempo word 0x2730 + 2s,
  volume 0x272e + s, tracks 0x272c + s, first virtual voice 0x2734 + s, voice mask 0x273a + s,
  priority 0x273c + s; virtual voice v: track pointer 0x2768 + 2v, length 0x2778 + 2v.
- Instruments 0x397a, 8 bytes: pulse width, SR, AD, filter cutoff, filter control, waveform;
  0-14 used (12, 13 noise), 15-31 free. Note-on (0x3c91) takes a free voice of the mask or
  steals one of lower priority. SID register shadow 0x38df-0x38f7, copied to 0xd400 every
  frame.
- RAM outside the payload: 0x0750/0x0751 (frame counter), reads of 0x0aab/0x0ac7.

Capture (`tools/mame_sound.lua`, MODE=capture): per command a file `cmd_XX.txt` with
`init ym|pokey <reg> <val>` (registers before the command), `ym|pokey <t> <reg> <val>` (t in
60 Hz frames since the command) and `game <t> <cmd> 0` (the game's own command, the
converter cuts there); MODE=log logs the main CPU's commands and 0x400390 changes, NOCOIN=1
when `mame_trace.lua` runs alongside (wrapper `dofile`s both).

```sh
# effects WIN=150, jingle candidates WIN=600; 24 / 4 commands per run (the practice race
# times out after about 3980 frames)
MODE=capture OUT=$D/out CMDS=1f,20,... WIN=150 FRAMES=5200 ... -autoboot_script $R/romconv/internal/marble/tools/mame_sound.lua
(cd $D/out && python3 -m zipfile -c $R/romszip/marble_sound.zip cmd_*.txt)
# goal: practice crossing y 63
MODE=log NOCOIN=1 OUT=$D/g0 MARBLE_LEVEL=0 MARBLE_PLACE=900:604:492:-36 \
  MARBLE_PLACE_V=900:0:131072 MARBLE_RACE_UNTIL=1000 ... -autoboot_script $D/both.lua
```

Remake: `romconv/internal/marble/marble_sid.py` builds the jingles and effects as songs of
the C64 player (appended to the payload, song table entries, a silent song, a 6502 stub that
starts an effect on slot 1 / voice 0 over the music); `marble_sound.{h,cpp}` maps commands to
song and effect requests; `marble::renderSid` plays them on SidPlayer. The logic sends the
arcade bytes at the ported sites (jump, bounce, fall, dizzy, break, shards, lost, steelie hit,
slinky catch, bird, acid, piston launch, time bonus, gate table), the flow at a race load (0x00
and the race music, no intro delay), time out (0x09, 0x3f) and game over (0x61). Converted:
16 effects (`EFFECTS` in marble_sid.py) and the 0x3f jingle; the race end pieces 0x17/0x18,
0x10, 0x16, 0x1a-0x1e, the aerial gate tones 0x47-0x52 and 0x5d/0x60 are ignored. Flash: 6860 B
(build 2026-10-13, compressed SID file 5532 B). Host test `test_marble_sound` (songs without
strays, effect over the music keeps voices 1-2, command mapping; SOUND_WAV_DIR=dir writes
listening WAVs), `test_marble_flow` (race music, time out jingle, break sound).

## Open questions

- Level objects: playfield patch decode of tag 0x19 rows. Section
  Level objects. Slot sprites: section Level object sprites (masks over slot MOs behind the
  marble not seen in traces).
- Aerial steelie differs from MAME a10 at frame 1629 (+0x08 vz -0x6000 in MAME, -0xc000 in the
  model; steelie state 0x23 at (487, 394), pistons rising), also without the aerial objects.
- Enemies (section Enemies): two player target choices 0x15fe6 (steelies and slinkies), no
  trace of a slinky pulling the marble (0x15316, octant 0x70-0xc0), of bird/piston hits
  or of a steelie occluded by terrain (masks +0x38).
- Deaths (section Player states, deaths, RNG): states 1/2/4/5/7/9 modelled, 1/2/4/5/7
  ported; 3 (tubes, catapult), 0xa (gate), 0xb (birds) and 8 (time bonus) modelled and
  ported. Not modelled: practice landing markers (type 0x2a, 0x12532, score 0x285b0), vblank
  IRQ work besides the race timer, two players, the main loop countdowns 0x400768 (sound
  only)/0x4003b8. Not ported to C++: dots 0x1c014 / 0x25b40.
- Goal (state 6): the camera ignores the marble (sy only from states 0/1/3/4/5).
- Game flow (section Game flow): the 0x16a20 time out summary and 0x18a88 final screen are
  frame counts only (texts and palette pulses: section HUD); the race intro scroll in and
  the high score entry (6c) are not ported.
- Object/enemy scripts, Phase 4.
