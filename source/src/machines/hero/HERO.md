# H.E.R.O. (Atari 2600) reverse engineering notes

Basis for a galagino remake: levels and graphics come from the ROM, the logic is
reimplemented in C++ (no 6502/TIA emulation). Status: the remake
(`source/src/machines/hero/`) reproduces every RAM byte it models frame by
frame on 13 MAME runs (see "Remake"). Facts marked **(verified)** were
confirmed by MAME RAM dumps or screenshots; everything else is read from the
disassembly.

## ROM

- File: `romszip/H.E.R.O..zip` -> `H.E.R.O..A26`, 8192 bytes,
  CRC32 `721e95b7`, MD5 `fca4a5be1251927027f2c24774a02160`.
  MAME softlist `a2600:hero` (Activision AZ-036-04, NTSC, 1984, John Van Ryzin).
- Bankswitching: F8 (2 x 4 KB). Hotspot read/write at `$1FF8` selects bank 0,
  `$1FF9` selects bank 1. Both banks are visible at `$1000-$1FFF` (and mirrors).
- The code is assembled with different origins per bank:
  - bank 0 (file `$0000-$0FFF`): origin `$D000`. Display kernel, sound,
    scoring, graphics.
  - bank 1 (file `$1000-$1FFF`): origin `$F000`. Reset, game logic, level data.
- Vectors: bank 0 reset/IRQ `$D000`, bank 1 reset/IRQ `$F000`. NMI vector
  (`$EAEA`) is filler.
- Bank trampolines (both banks, same file offsets):

  ```
  bank 1  $FFEC  bit $FFF8        ; -> bank 0, continues at $DFEF: jmp $D079
  bank 0  $DFF2  bit $FFF9        ; -> bank 1, continues at $FFF5: jmp $F042
  ```

  Bank 1 runs the frame logic, ends with `jmp $FFEC` (`$F62D`), bank 0 draws
  the frame and returns through `$DFF2`.
- Static disassembly reaches 4470 of 8192 bytes as code, no indirect jumps.

## Frame structure

1. Bank 1 `$F042`: read joystick, console switches, attract demo, game logic,
   room decode (every frame, not only on screen change), sprite setup.
2. Bank 0 `$D079`: score/level digit pointers, wait for timer, display kernel,
   sound (`$D58C-$D636`), overscan logic, back to bank 1.

## Levels

- 20 levels, each a linear chain of screens (no branching). The hero leaves a
  screen at the bottom (falling) or at the left/right edge (horizontal parts).
- Level variable `$F5` (0-based), screen index within the level `$9C`.
- `room = $FF00[level] + screen` **(verified)**, `$F630` computes it.

| Table   | Content (20 bytes, one per level) |
|---------|-----------------------------------|
| `$FF00` | first room: `00 02 06 0c 14 1c 26 32 40 50 60 70 80 90 a0 b0 c0 d0 e0 f0` |
| `$FF14` | miner room: `01 05 0b 13 1b 25 31 3f 4f 5f 6f 7f 8f 9f af bf cf df ef ff` |
| `$FF28` | miner screen: `01 03 05 07 07 09 0b 0d 0f ...0f` (copy at `$D065`) |
| `$D02A` | displayed level number, BCD, `$A` = blank digit: `a1..a9 10..20` |
| `$D03D` | power bonus, low BCD byte per level (see below) |
| `$D051` | power bonus, high BCD byte per level |

Screens per level: 2, 4, 6, 8, 8, 10, 12, 14, then 16 for levels 9-20. Total
256 rooms, so one byte indexes every room.

### Room record

8 parallel tables of 256 bytes in bank 1, `$F700-$FEFF` (2 KB), indexed by room.
The screen is drawn as a top cap plus 3 horizontal bands (top, middle, bottom).

| Table   | RAM   | Meaning |
|---------|-------|---------|
| `$F700` | `$A5` | bottom band object: x position (bits 7-2), type (bits 1-0); `$93` = none |
| `$F800` | `$A6` | middle band object, same encoding |
| `$F900` | `$A7` | top band object x (bits 7-2, type forced to 1, `$91` -> `$93` = none); bit 0: side where the level continues (compared with `$FFAA`) |
| `$FA00` | `$C0` | middle band PF1 |
| `$FB00` | `$BF` | bottom band PF1 |
| `$FC00` | `$C3` | middle band PF2 |
| `$FD00` | `$C2` | bottom band PF2 |
| `$FE00` |       | flags: bit 0 -> object width (NUSIZ/CTRLPF for band), bit 1 -> lava colour animation (`$FF3C`: `42 44`), bits 7-2 -> special object (miner `$08`, `$09`/`$99` special cases, raft/tentacle, see open items) |

Top band PF1/PF2 (`$C1`/`$C4`) **(verified)**:

- screen 0: PF1 `$C0`, PF2 `$00` (shaft entrance)
- screen >= 11: `$FF`/`$FF` (solid rock above horizontal tunnels)
- otherwise: the previous room's bottom band (`lda $FAFF,y` wraps into
  `$FB00[room-1]`, same for `$FCFF,y` -> `$FD00[room-1]`)

So consecutive screens share one band: the bottom of screen n is the top of
screen n+1.

Example, level 1 (rooms 0-1):

```
room  F7 F8 F9 FA FB FC FD FE
  0   93 93 90 00 ff 00 3f 00
  1   93 30 90 c0 ff 00 ff 08   ; wall object at x=$30 in middle band, miner
```

### Playfield

- Reflected playfield (CTRLPF bit 0 set, bit 2 priority), so wall shapes
  are mostly symmetric: 20 bits per half (PF0 4, PF1 8, PF2 8), each bit
  4 colour clocks. Asymmetric details (thin destructible walls) are P0 objects.
- PF0: top and bottom band `$FF`. Middle band `$BE | $D9E3` (`$D9E3` = 0):
  `$BE` = `$00` on screens >= 10 and in non-miner rooms whose `$FE` gives an
  edge ball (`$09`/`$99`), else `$FF` **(verified)**.
- `$FE` bit 0 clears CTRLPF reflect for the middle band: its right half
  repeats the left instead of mirroring it **(verified)**.
- Ball (TIA BL, PF colour, 8 px, enabled for the whole band area, hidden
  behind playfield, P1 vs BL only pushes the hero sideways): the thin
  destructible wall, x = `$A0` - 1. Set on screen change (`$F66F-$F6CB`):
  - back to the previous screen (`$9D`): `$A0-$A4` swapped with the saved
    copy `$ED-$F1` (wall, critter states, hue/darkness kept)
  - frontier screen (first visit, `$9E` = `$9C`): `$A0` = `($FE & $FC) | 1`
    if `$FE & $FC` != 0, else 0; critters alive (`$A1-$A3` = 0)
  - any other older screen: only edge walls stay (`$A0` = `$09` x 8 or `$99`
    x 152, else 0), critters gone (`$A1-$A3` = `$32`)
  - level start (`$D9F1`): `$A0` = `$3D`, wall at x 60 on screen 0
  **(verified: MAME render of all 256 rooms with the first-visit `$A0`)**.
- Band colour: `$FFAE[band] | $A4` (`$FFAE = 06 04 02`), `$A4 = $FF6A[level & 3]`
  (`20 c0 90 00`). Lava rooms flash between `$42`/`$44`.
- Moving wall: on levels >= 14, when middle band PF1 is `$FF` and PF2 is `$7F`,
  PF2 cycles through `$FFB8` (`0f 1f 3f 7f ff 7f 3f 1f`).
- Line layout (line 0 = MAME a2600 snapshot row 26) **(verified)**:
  - 2-6: top cap, kernel x = 4..0: PF0 `$DC66[x]`, PF1/PF2 = top band &
    `$DC61[x]`, colour `$DD09[x] | hue`
  - 7-46: top band, last line dark (bare hue)
  - 47-85: middle band, first and last line dark
  - 86-124: bottom band, first line dark
  - 125-128: floor: PF0/PF1/PF2 = bottom band & wave byte, wave = page `$DC`
    at `$DA1D[n]` (n = `($81 >> 2) & 7` on the miner screen and screens
    >= 10, else 0), colour `$DD0A[y] | hue`, background water `$DD01[level &
    3]` on those screens, else `$E1`
  - 129-175: HUD on grey playfield (colour `$04`)
- Hue: `$FF6A[level & 3]` (copy at `$DD13`), set at level start and screen
  change; band colour `$FFAE[band] | hue` (`06` bottom, `04` middle, `02` top).
- Palette: MAME `tia_ntsc_video_device::init_palette()` (phase shift 26.2)
  reproduces every snapshot colour exactly.
- Room check: `romconv/dev/hero/roomcheck.py` matches 768 of 768
  bands (256 rooms x 3) with MAME, the animated middle band of moving-wall
  rooms excluded.
- 2600 playfield is 160 pixels wide; H.E.R.O. blanks the left 8 (HMOVE bar).

### Band objects

Each band has a first sprite (P0, 13 rows, graphics page `$DB`, colours page
`$DD`) and a second one (rows 12..1, graphics page `$DA`, colours `$DD`).
Index 0 = bottom band, as in the original. **(verified by the traces)**

- First sprites, x `$A5-$A7` reloaded every frame from `$F7`/`$F8`/`$F9`
  (`& $FC`; top `| 1`, `$91` -> `$93`; `$93` = none, graphics `$8A`).
  - Top band (`$F9`): always the lamp, graphics `$64`, colour `$8E`. Shot
    (M1 vs P0) or touched (P1 vs P0): `$A4` = 1, darkness.
  - Middle/bottom (`$F8`/`$F7`): critter, type = bits 1-0, graphics
    `$85+type`, colour `$89+type` (`4D 5A 33 17`). Frames from the critter
    clock `$F6`: types 0, 1, 3 `$FF8B`/`$FF8D`/`$FF9B[($F6 >> 3) & 1]`,
    type 2 `$FF8F[($F6 >> 2) & 3]`.
  - Motion (`$F3AF`) from the oscillators `$EA`/`$EB` (`& $7F`): type 0
    static, type 1 moves up by `$EA` (`$A1,x` = offset), type 2 up by `$EA`
    and right by `$EB`, type 3 right by `$EA` (9 while hit).
  - `$A1,x`: vertical offset, `$32` = gone (never drawn: the kernel draws a
    row only when line index - offset < 13).
  - Hit (`$F7,x` = `$13` timer): graphics `$76` ("50"), colour `$40`; at 0
    the critter is gone. `$F6` stops while a bottom or middle critter is
    dying, during a death and after the rescue.
- Oscillators (`$D51A`): `$EA` bounces 0..7, `$EB` 0..`$16`, bit 7 = rising,
  one step when `$F6 & mask` = 0, mask 3 (levels 1-9), 1 (10-17), 0
  (18-20). `$EC` (raft, 0..`$60`) steps only while the hero stands at y
  `$17`.
- Second sprites, x `$A8-$AA`, `$93` = none (graphics `$99`):
  - dynamite: graphics `$FF93[$E7]` (sparkling fuse), colour `$68`
  - explosion (fuse < `$20`): `$FF96[(fuse >> 1) & 3]`, or `$8B` once a wall
    went (`$FA`), colour `$40`; background `$E1` = `$FFA5[...]` (`$0A` on
    one frame of four: the flash, which also freezes the hero)
  - miner (miner screen, middle band): x `$80`, graphics `$71` (`$7E` once
    rescued), colour `$26`. When the miner room's `$FE & $FC` is 8 the miner
    stands at x `$18` and REFP0 mirrors every P0 of the frame (hit graphics
    `$6D` instead of `$76`).
  - raft (levels 10+, screens >= 11, bottom PF2 0 and PF1 `$C0`): graphics
    `$97`, colour 5, x `($EC & $7F) + $1C`. Standing on it (P0 vs P1 in the
    bottom band, x within 9 of `($EC & $7F) + $18`) carries the hero
    (`$F106`).
  - tentacle (levels 17+, screens >= 11, bottom PF2 0 and PF1 `$E0`):
    graphics `$F6D2[($F6 >> 3) & 7]`, colour `$C4`; follows the hero's x
    within `$1C..$78` every 4th frame (2nd from level 19); touching it
    overrides the joystick towards it (`$F14B`).
- Darkness (`$A4` bit 0): band colours 0 (2 while a fuse burns in band 1 or
  2), critter colours `$9B` unless hit or a fuse burns; cap and floor keep
  their luminance. No lava death in the dark (the lava test reads the band
  colour). Reset by the next screen change, kept by the swap with `$ED-$F1`.
- Only the current and the previous screen keep their state.

### Horizontal screen change (`$F236`)

Hero x `$9B` < `$0C` (left) or >= `$93` (right). If the exit direction matches
`$F900[room] bit 0` the screen index decreases, otherwise it increases. The
hero reappears at x `$FFAC[dir]` (`93`, `0f`). `$9E` tracks the furthest screen.

## Hero movement **(verified by the traces)**

- Position: x `$9B` (playfield pixel of the sprite's left edge, range
  `$0C`..`$92`), y `$9F` (larger = higher). The sprite's top (sensor) row is
  on source line 121 - `$9F` (MAME snapshot row 147 - `$9F`) **(verified,
  standing and hovering hero)**: the kernel shows row Y of its line counter
  on line 123 - Y, and the pointers put the top row at Y = y + 2.
- Sprite (P1, `$F573`): 26 rows from ROM address `$DE00 + X + $19` (top) down
  to `$DE00 + X` (bottom), colours `$DD8D` down to `$DD74`. Bit 0 of a colour
  byte enables the row (GRP1), bit 1 enables M1 (laser). Frames are 25 bytes
  apart: `X` = `$81`, `$9A`, `$B3`, `$CC`, `$E5`; flying `$FFA2[$E7]`
  (`$E7` 0..2), walking `$FF9D[$E8]` (`$E8` 0..4, page `$DC` when standing).
  Each frame's top and bottom row is an 8 px bar in colour `$01` (black, row
  enabled): invisible, but the TIA counts collisions on black pixels, so they
  are the head and feet sensors. Pointers: `$91` = X - (`$9F` - `$17`),
  `$95` = `$74` - (`$9F` - `$17`).
- Vertical (`$F184`): thrust `$E9` 0..`$3F`: +1 per frame while UP is held,
  -1 otherwise (floor 0, cap `$3F`). Speed index `i = $E9 >> 3` (on screens
  >= 11 `i` is at least 1); `i < 4` sinks by `$FFB0[i]`, `i >= 4` rises by
  `$FFB0[i]`, `$FFB0` = `02 01 00 00 00 00 01 02`. Rising is blocked while
  `$E6` bit 7 is set (ceiling), sinking while bit 6 is set (standing).
- Screen change up (`$F1C0`): y >= `$8A` -> previous screen, y = 4 (screen 0:
  clamp y `$8A`). Down (`$F1F4`): y < 0 -> next screen, y = `$89`. On screens
  >= 10, sinking below y `$16` is death by water (`$AD` = `$FF`).
- Horizontal (`$F20E`): a step starts only when `x & 3 == 0`, then continues
  to the next multiple of 4, 1 px per frame. Facing `$B0` (0 right, 8 left,
  also REFP1). Turning around costs a 3 frame pause (`$FC`) unless standing.
  Left of `$0C` / right of `$93` changes screen (see "Horizontal screen
  change").
- Collision (`$D817`, overscan, bank 0) from the TIA latch P1 vs playfield
  per band (`$E2-$E4`, bit 7; bit 6 = P1 vs ball):
  - y equals `$D9E9[band]` (or 1 below): land, y snapped, `$E6` = `$40`,
    `$AC` = `$40` (idle) or `$80` (walking, joystick left or right)
  - y equals `$D9EC[band]` (or 1 above): ceiling, y snapped, `$E6` = `$80`
  - else: side hit, x pushed back 1 px against the facing, once per frame
  - band index 0 bottom, 1 middle, 2 top; `$D9E9` = `3c 63 ef` (floor y),
    `$D9EC` = `ef 24 4b` (ceiling y; `$EF` = never). So the hero only stands
    on and bumps into band boundaries; walls fill whole band heights.
  - latches: `$E2` = band 0, read after the kernel (`$D335`); `$E3`/`$E4` =
    bands 1/2, read at the start of the band below (`$D1A0`, `sta $E3,x`) and
    cleared there (CXCLR); the remake's windows are under "Remake".
- Respawn glide targets `$DAFC[x]` = `22 49 70 1c`.
- Respawn / level start (`$AD` != 0, `$D7B6`): the hero glides down 1 px per
  frame to y `$DAFC[x]`, then waits; the first joystick move with `$AF` set
  clears `$AD` and sets `$E9` = `$1F` (hover). After `$D7B6` the kernel
  leaves `$E6` = `$FF` (both directions blocked) until the next collision
  pass.

## Remake (source/src/machines/hero)

- `hero_game` keeps the original's state under its RAM addresses (`ram()`
  maps `$80-$FF`); `tick()` is one frame: the previous overscan's second
  half (`$D717-$D9DF`), the frame logic (bank 1), the kernel's collisions,
  the overscan's first half (`$D51A-$D6E8`). The state after `tick()` equals
  a MAME RAM dump taken when the game increments `$81` (`$D700`, harness
  `H_RAM_TAP`); MAME's own frame boundary drifts against the game loop.
- Host test `source/test/test_hero_game`: 13 MAME runs
  (`romconv/dev/hero/traces/`, `make_fixture.sh`) replayed from a
  loaded RAM state with the recorded SWCHA/INPT4/SWCHB; every modelled RAM
  byte must match every frame (collision latches on their used bits). Runs:
  walking/flying, shaft, level 1 with dynamite and a death, laser kill,
  rescue and bonus count into level 2, critters and darkness on level 10,
  raft, tentacle, moving wall, power out, game over, the 1,000,000 stop,
  the attract demo (3000 frames), game select and RESET.
- `hero_video` draws from the display registers the frame logic prepares
  (`VideoState`); MAME snapshots of the HUD, playfield, critters, miner and
  dynamite match pixel for pixel.
- Collisions: P1 vs playfield/ball (`$E2-$E4`) per hero row: row k counts
  for band i when y + 24 - k is in [21 + 39 i, 60 + 39 i); rows with colour
  bit 0 clear or empty graphics never collide (the death and flash sprites
  differ from the walking ones). Floor (`$3C`, `$63`) and ceiling (`$24`,
  `$4B`) thresholds, the top exit `$8A` and the water line `$16` follow from
  that. The other latches are rasterised from the kernel's lines:
  - band lines: top band L0 = line 7, band s (0 top) L1..L39 = lines
    8 + 39 s .. 46 + 39 s
  - first sprite row r at line 30 + 39 s - offset - r, second sprite row r
    (12..1) at line 46 + 39 s - r, x = pixel of the left edge
  - P0 vs P1 `$D1-$D3`: band lines L1..L23 (read at `$D263`); M1 vs P0
    `$D4-$D6`: L1..L24 (`$D2B0`); P0 vs P1 of the whole bottom band for the
    raft and tentacle (CXPPMM read by bank 1)
  - M1 (laser): hero row 5 (colour `$47`, bit 1), only on band lines L2..L38
    (ENAM1 is not written on L0, L1, L39); it collides at pixel position - 1
    (as the ball). MAME draws the laser 4 px further right than it
    collides; the remake draws it where it collides.
  - M1 vs playfield/ball `$F4` accumulates from line 7 on until the laser
    restarts; when not firing M1 sits at position 1 and usually latches the
    left wall.
- Animation: `$E7` (propeller, 2..0) steps on frames with `$81` bit 1 set,
  `$E8` (walk, 4..0) every 4 frames (`$D56C`); both hold while `$F2` is set.

## Overscan rules (bank 0 `$D717-$D9DF`) **(verified by the traces)**

Skipped while RESET is held, the screen saver blanks (`$B4` bit 7) or no
game runs (`$BA`). Rescued (`$F2`): level end below. Respawning (`$AD` != 0):
the death sequence, then explosion and power fill only. Otherwise, in order:

- Collision (`$D817`, not during the flash `$E1`): land, ceiling, side push
  (see "Hero movement"). Landing while walking restarts the walk at frame 2.
- Per band (`$D889`): laser hit (M1 vs P0, no hit timer): lamp -> dark, else
  50 points and the hit timer. Touch (P0 vs P1, no hit timer): lamp -> dark,
  else death. P1 vs playfield or ball in a band coloured `$4x` (lava): death,
  band by y (>= `$63` one up, < `$25` one down).
- Dynamite (`$D8E3`): standing, dynamite left, none in that band yet (2 if
  y >= `$62`, else 1), joystick exactly DOWN (`$D0`), x a multiple of 4 and
  >= `$0D`: at the hero's x, fuse `$40`, counted down by bank 1.
- Rescue (`$D912`): on the miner screen (`$D065[level]`), hero x - (`$A9` -
  7) < 15 (no y test): 1000 points, `$F2` = `$FF`.
- Power (`$D932`): after the first fill (`$AF` = `$FF`) 1 unit per 64
  frames; empty: the level restarts (`$D9F1`) with a death.
- Explosion (`$D94B`) at fuse `$1F`: one dynamite used. Critter with x in
  (`$A5` - `$13`, `$A5` + `$10`]: hit timer, 50 points. Wall `$A0` < `$99`
  with x in (`$A0` - `$0F`, `$A0` + `$0C`]: gone, 75 points, `$FA` = `$75`.
  Hero with y in [`$DBFC,x`, `$DAFF,x`) (`15 3C 63`, `1C 43 6A`) and x -
  (dynamite - 8) < 17: pushed 1 px, death.
- Power fill (`$D9C7`, `$AF` = 0, `$AD` < `$77`): +1 per frame up to `$51`,
  then `$AF` = `$FF`. Only at level start: after a death the hero keeps the
  power left.
- Death sequence (`$D7B6`, `$AD` = `$FF`, band `$AE`): 105 frames with the
  death sprite (`$DAE2`), drowning sinks 1 px per 8 frames; at `$96` the
  hero goes to y `$8C`, the killer's band object is removed, one life is
  lost (not in the demo; 0 = game over); then the glide to `$DAFC[$AE]`
  and the wait for a joystick move.
- Level end (`$D735`): each power unit pays `$D051:$D03D[level]` (BCD,
  hundreds:units) every 2nd frame (every 4th from level 7), then each
  dynamite 50 points (16 frames apart, timed with the band 1 fuse), then
  the next level (`$D777`).

The overscan's first half (`$D51A-$D6E8`, before `$81` increments):
oscillators, animation, sound, moving wall phase `$FD` (+1 & `$3F` per
frame from `$50` at screen entry, stopped and wound back by a death), laser.

- Laser (`$D65C`): while FIRE is held (always in the demo), not respawning,
  not rescued, and y not in {`$75`, `$76`, `$4F`, `$50`, `$28`, `$29`,
  `$77`} (`$D9E5`, the band edges where ENAM1 is off). `$F4` bit 7 (M1 vs
  playfield): the beam restarts (`$F3` = 0). Bit 6 (M1 vs ball) with a
  mid-screen wall (`$0A` <= `$A0` < `$99`): on even frames `$B1` + 1; at
  `$80` the wall narrows to 4 px (`$A0` + 4 if right of the hero), at 0 it
  is gone (512 frames). Else `$F3` = (`$F3` + 3) & 15. Position: facing
  right x + 7 + `$F3`, left x - 5 (+ 4 when narrow) - `$F3`; >= `$9F`: 0.
  Width = ball width (`$FF3E[$B1 >> 6]`, 8 or 4 px). Turning resets `$F3`.

## Game rules **(verified by the traces)**

- Game number `$80` (bits 6-0), start level `$FF6D[game]` = 0, 4, 8, 12, 16
  (Game 1-5 start at level 1, 5, 9, 13, 17).
- Level advance (`$D777`):
  - Game 5: random level, `r = $82 & $1F` (LFSR), `r < 20 ? r : r - 12`.
  - Otherwise `level + 1`; after level 20 set `$80` bit 7 and continue at
    level 13. Levels 13-20 loop forever ("LEVEL:" then shows glyphs `$D8`/
    `$E0` instead of the number).
  - Attract demo: `level & 3`.
- Score `$B7-$B9` BCD (`$DBB2` hundreds, `$DBBA` units; a value >= `$99`
  adds one more). Extra life when bit 1 or 4 of `$B7` changes (every
  20,000), at most 7 (`$B3`, 4 at start = 1 + 3 reserve). A carry out of
  the top byte stops the game at 1,000,000: score `$BBBBBB`, game over.
  No points in the demo.
- Console (`$F042`): RESET clears RAM from `$B4` and starts game `$80`;
  SELECT (repeat every 30 frames) steps `$80` (wraps after Game 5), stops
  the game and shows the game number as the score. The attract timer `$B6`
  counts down every 8 frames from `$1F` (after SELECT) or `$FF` (game
  over); at 0 the demo starts: RAM from `$BA` cleared, level 1, joystick
  from `$FF42[room]`, DOWN near a wall to drop dynamite, `$FF56[room]`
  (away) while its fuse is >= `$2C`, laser always on, silent.
- Idle timer `$B4`: +1 per 2048 frames without joystick input, bit 7 blanks
  the screen (`$D104`).
- Remake controls: START is RESET, UP/DOWN outside a game is SELECT.
- From the manual: 6 dynamite per level, power drains once the hero moves.

## RAM (partial)

| Addr        | Meaning |
|-------------|---------|
| `$80`       | game number, bit 7 = looping past level 20 |
| `$81`       | frame counter |
| `$82`       | random LFSR |
| `$83`       | SELECT repeat delay |
| `$84`       | joystick (SWCHA high nibble) or demo input |
| `$85-$90`   | pointers for score digits / object sprites (kernel scratch) |
| `$91/$92`   | hero gfx pointer, `$95/$96` hero colour pointer |
| `$93-$9A`   | P0 gfx/colour pointers used by the kernel |
| `$9B`       | hero x |
| `$9C`       | screen index in level **(verified)** |
| `$9D`/`$9E` | previous / furthest screen |
| `$9F`       | hero y |
| `$A0`       | ball wall x + 1, 0 = none |
| `$A1-$A3`   | band object vertical offset, `$32` = gone |
| `$A4`       | level hue, 1 = dark |
| `$A5-$A7`   | band object x (bottom, middle, top) **(verified)** |
| `$A8-$AA`   | second object x (tentacle, raft) |
| `$AB`       | power |
| `$AC`       | standing: `$40` idle, `$80` walking |
| `$AD`/`$AE` | death/respawn counter, band |
| `$AF`       | power filled (drain on) |
| `$B0`       | hero facing (REFP1) |
| `$B1`       | laser wear of the wall |
| `$B2`       | dynamite left |
| `$B3`       | lives (incl. the current one) |
| `$B4`/`$B5` | idle timer, demo restart flag |
| `$B6`       | attract demo timer, 0 = game running |
| `$B7-$B9`   | score, BCD (`$A` = blank digit) |
| `$BA`       | no game running |
| `$BE`       | PF0 mask |
| `$BF-$C1`   | PF1 bottom/middle/top **(verified)** |
| `$C2-$C4`   | PF2 bottom/middle/top **(verified)** |
| `$C5-$C7`   | band playfield colours |
| `$C8-$CA`   | band CTRLPF |
| `$DE-$E0`   | dynamite fuses |
| `$E1`       | background colour (flash) |
| `$E2-$E4`   | P1 vs playfield/ball latches |
| `$E6`       | kernel band counter, then floor/ceiling flags |
| `$E7`/`$E8` | propeller / walk animation |
| `$E9`       | vertical thrust / velocity |
| `$EA-$EC`   | critter and raft oscillators |
| `$ED-$F1`   | saved `$A0-$A4` of the other screen |
| `$F2`       | level complete (miner reached) |
| `$F3`/`$F4` | laser phase, M1 latches |
| `$F5`       | level, 0-based **(verified)** |
| `$F6`       | critter clock |
| `$F7-$F9`   | critter hit timers |
| `$FA`       | wall blown by this explosion |
| `$FB`       | floor wave frame (SWCHB copy during the overscan) |
| `$FC`       | turn delay |
| `$FD`       | moving wall phase |

## Display kernel (bank 0)

- `$D00C`: coarse/fine horizontal positioning (`RESPx` + `HMPx`).
- `$D12C`: top cap, 5 lines.
- `$D17A-$D309`: one iteration per band, 3 bands, 39 lines each (L1..L39):
  - L1: PF1, PF0, PF2, CTRLPF; CXP1FB of the band above stored (`$E3,x`),
    CXCLR. L2: band colour.
  - L3: first P0 positioned; L5..L23: first P0 object (`($93),y` gfx,
    `($97),y` colour), row = window line - `$A1,x`
  - CXPPMM stored after L23 (`$D1,x`), CXM1P after L24 (`$D4,x`); L24/25:
    P0 repositioned, L26..L38: the second sprite (`($99),y`)
  - L39: dark line (COLUPF = `$A4`), PF0 of the next band
  - P1 (hero) from `($91),y`/`($95),y` on every line, row Y on line 123 - Y;
    ENAM1 from the colour byte on L2..L38 only
  - CXM1FB ORed into `$F4` at each band start and after the kernel; CXP1FB of
    the bottom band into `$E2` after the kernel
- `$D30C`: floor waves (4 lines), then the HUD (`$D373`) and logo (`$D4C0`).

## HUD (`$D3A0`, source lines 129-175, grey `$04`) **(verified)**

- 132-136: "POWER" (`$DE72`/`$DE77`/`$DE7C`, 24 px at pixel 24, colour
  `$0C`); bar yellow `$1C` from pixel 49 for `$AB` px, red `$42` up to
  pixel 127 (a full bar ends at 127).
- 139-151: reserve lives (`$B3` - 1) as hero icons (`$DDA9`, colours
  `$DDB7`); 153-163: dynamite (`$DA34`, colours `$DD69`). 6 slots 8 px
  apart from pixel 56, filled from the right (playfield masks
  `$DC6A`/`$DC6C`/`$DC77` hide the left ones).
- 166-173: 6 glyphs from page `$DF` at pixel 56 (`$D079`): the score with
  leading blanks, or "LEVEL: n" (`$CD`, `$EF`, `$D02A[level]`, yellow) until
  the power is full; after a game over it blinks with the score.
- 179-187: the copyright text (`$DFA8..`) and the Activision logo (`$DF80`,
  5 bytes at pixel 64, rainbow of PF2 and the ball in `$DDEC[row]`). The
  attract mode flips from one to the other (`$D47F`).

## Graphics

All sprite data is in bank 0, `$DA00-$DFFF`, stored upside down (the kernel
counts lines down): hero frames, critters, lamp, miner, raft, tentacle,
dynamite, explosions, digits and letters, "POWER", copyright text, logo,
colour tables (`$DB..`, `$DD..`).

## Sound (`$D58C-$D636`)

Two TIA channels, no music, silent in the demo:

- Channel 0, `AUDV0` 5: laser while FIRE is held and `$F3` bit 7 is clear
  (`AUDC0`/`AUDF0` `$DB9F`/`$DBA3[$81 & 3]`); death (`$AD` >= `$96`,
  `AUDV0` = (`$AD` - `$60`) >> 4, `AUDC0` `$DBA0[($81 >> 1) & 1]`, `AUDF0`
  `$0F`); bonus count beeps (`$DBA7`/`$DBAB[($81 >> 1) & 3]`).
- Channel 1, `AUDC1` 8 (9 bit poly): burning fuse in band 1/2 (`AUDF1` =
  `AUDV1` = random 0-3), explosion (`AUDV1` = fuse >> 1, `AUDF1` `$10`),
  power fill (`AUDF1` = ~(`$AB` >> 2), `AUDV1` 4), propeller in the air
  (`AUDF1` `$DBAF[$E7]`, `AUDV1` `$E7` + 1).
- Remake: each TIA channel drives a POKEY channel at 64 kHz: pure tones
  (AUDC 4 / `$C`: 31.4 kHz / (AUDF + 1) / 2 or 6) and the 9 bit poly at the
  same rate. Per effect within a factor of two of MAME's loudness.

## Open items

- `$FE` bits 7-2 beyond the walls and the miner side.
- Game 5's random levels and the screen saver are not covered by a trace.
- MAME shows the laser 4 px right of where it collides (see "Remake").

## Background

- [AtariAge H.E.R.O. maps by Adam Lewis](https://atariage.com/2600/archives/HEROMaps/index.html):
  maps of all 20 levels, useful to verify the room decoder.
- [AtariAge manual](https://atariage.com/manual_html_page.php?SoftwareLabelID=228):
  scoring, lives, game variations.
- [Hardcore Gaming 101](http://www.hardcoregaming101.net/hero/): gameplay
  overview; its "levels 13-20 loop" matches the code.
- [Wikipedia](https://en.wikipedia.org/wiki/H.E.R.O._(video_game)).

## Tools used

In `romconv/dev/hero/`:

- `dis6502.py`: recursive 6502 disassembler for F8 carts (follows bank
  switches through hotspot accesses, origins `$D000`/`$F000`).
- `harness.lua`, `run_mame.sh`: headless MAME `a2600` with scripted input,
  RAM pokes, RAM dumps, bank aware ROM read log, snapshots.
- `capture_rooms.py`: snapshots of all 256 rooms (pokes `$F5`, `$9C`, `$A0`).
- `trace_run.py`: per-frame RAM CSV of a script; `trace_fixture.py` and
  `traces/make_fixture.sh`: the host test fixture (delta-coded records of RAM,
  SWCHA, INPT4, SWCHB at the `$81` increment).
- `roomcheck.py`: decode check against the snapshots, writes the host test
  fixture `source/test/test_hero_room/hero_mame_bands.h`.
- `gfxdump.py`: ROM bytes as bitmap columns, to find sprites.
- `make_logo.sh`: `logos/hero.png` from a level 1 snapshot.
