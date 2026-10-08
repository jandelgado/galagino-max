# H.E.R.O. (Atari 2600) reverse engineering notes

Basis for a galagino remake: levels and graphics come from the ROM, the logic is
reimplemented in C++ (no 6502/TIA emulation). Status: research. Facts marked
**(verified)** were confirmed by MAME RAM dumps or screenshots; everything else
is read from the disassembly and still needs checking.

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
  behind playfield): x = `$A0` - 1. `$A0` = `($FE & $FC) | 1` if that is `$09`
  (x 8) or `$99` (x 152), else 0 (off screen), set on screen change
  (`$F6A7`). Level start (`$D9F1`) loads `$A0` = `$3D`: screen 0 always has
  a wall at x 60..67 next to the start position **(verified)**.
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
- Room check: `romconv/internal/hero/tools/roomcheck.py` matches 768 of 768
  bands (256 rooms x 3) with MAME, the animated middle band of moving-wall
  rooms excluded.
- 2600 playfield is 160 pixels wide; H.E.R.O. blanks the left 8 (HMOVE bar).

### Band objects

- Object x positions `$A5-$A7`, per band state `$A1-$A3`
  (`$32` = destroyed/inactive), "being lasered" timers `$F7-$F9`.
- Type (`$F700`/`$F800` bits 1-0) selects the sprite pointer `$85+type` and
  colour pointer `$89+type` (`$89-$8C = 4d 5a 33 17`, page `$DB`/`$DD`):
  - 0: static
  - 1: static, state from `$EA` (animated)
  - 2: moves, x += `$EB`
  - 3: moves, x += `$EA` (or 9 while hit)
- Second sprite per band: `$D8-$DA` gfx, `$DB-$DD` colour (lamp, explosion
  frames from `$FF96`, enemy animation from `$FF93`).
- Tentacle/raft object `$A8`: follows the hero's x between `$1C` and `$78`
  (levels >= 17, `$F4C0`).

### Horizontal screen change (`$F236`)

Hero x `$9B` < `$0C` (left) or >= `$93` (right). If the exit direction matches
`$F900[room] bit 0` the screen index decreases, otherwise it increases. The
hero reappears at x `$FFAC[dir]` (`93`, `0f`). `$9E` tracks the furthest screen.

## Hero movement (from code, partly verified)

- Position: x `$9B` (playfield pixel of the sprite's left edge, range
  `$0C`..`$92`), y `$9F` (larger = higher). The sprite's top (sensor) row is
  MAME snapshot row 145 - `$9F` (source line 119 - `$9F`, +-1 for the frame
  the snapshot shows) **(verified)**.
- Sprite (P1, `$F573`): 26 rows from ROM address `$DE00 + X + $19` (top) down
  to `$DE00 + X` (bottom), colours `$DD8D` down to `$DD74`. Bit 0 of a colour
  byte enables the row (GRP1), bit 1 enables M1 (laser). Frames are 25 bytes
  apart: `X` = `$81`, `$9A`, `$B3`, `$CC`, `$E5`; flying `$FFA2[$E7]`
  (`$E7` 0..2), walking `$FF9D[$E8]` (`$E8` 0..4, page `$DC` when standing).
  Each frame's top and bottom row is an 8 px bar in colour `$01` (black, row
  enabled): invisible, but the TIA counts collisions on black pixels, so they
  are the head and feet sensors. Pointers: `$91` = X - (`$9F` - `$17`),
  `$95` = `$74` - (`$9F` - `$17`), kernel line counter Y = 147 - snapshot row
  **(verified at y `$6A`)**.
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
  - y equals `$D9E9[band]` (or 1 above): land, y snapped, `$E6` = `$40`,
    `$AC` = `$80` (standing)
  - y equals `$D9EC[band]` (or 1 below): ceiling, y snapped, `$E6` = `$80`
  - else: side hit, x pushed back 1 px against the facing, once per frame
  - band index 0 bottom, 1 middle, 2 top; `$D9E9` = `3c 63 ef` (floor y),
    `$D9EC` = `ef 24 4b` (ceiling y; `$EF` = never). So the hero only stands
    on and bumps into band boundaries; walls fill whole band heights.
  - latches: `$E2` = band 0, read after the kernel (`$D335`); `$E3`/`$E4` =
    bands 1/2, read at the start of the band below (`$D1A0`, `sta $E3,x`) and
    cleared there (CXCLR). Exact latch windows still to verify with traces.
- Respawn glide targets `$DAFC[x]` = `22 49 70 1c`.
- Respawn / level start (`$AD` != 0, `$D7B6`): the hero glides down 1 px per
  frame to y `$DAFC[x]`, then waits; the first joystick move with `$AF` set
  clears `$AD` and sets `$E9` = `$1F` (hover).

## Game rules (from code)

- Game number `$80` (bits 6-0), start level `$FF6D[game]` = 0, 4, 8, 12, 16
  (Game 1-5 start at level 1, 5, 9, 13, 17).
- Level advance (`$D777`):
  - Game 5: random level, `r = $82 & $1F` (LFSR), `r < 20 ? r : r - 12`.
  - Otherwise `level + 1`; after level 20 set `$80` bit 7 and continue at
    level 13. Levels 13-20 loop forever.
  - Attract demo (`$B6 != 0`): `level & 3`.
- Attract demo replays joystick values per room from `$FF42` (`$70` = right,
  `$B0` = left, SWCHA high nibble).
- Rescue bonus: for each remaining dynamite `$B2` add 50 (`$D769`), power
  bonus per remaining unit `$AB`: `$D051[level]:$D03D[level]` BCD, i.e. 20, 40,
  60, 80, 90, 100, 120, 160, 200, 240, 280, 320, 360, 400, 420, 440, 500, 540,
  600, 999 (byte order to verify).
- Initial state block `$FF72-$FF8A` (25 bytes) is copied to `$9B-$B3` on reset.
- From the manual: critter 50, wall with dynamite 75, miner 1000, dynamite left
  50 each, power bonus; 6 dynamite per level; 4 lives (1 + 3 reserve); extra
  life every 20,000, at most 6 reserve; game stops at 1,000,000. Power starts
  draining on the first joystick move; when it runs out the hero dies.

## RAM (partial)

| Addr        | Meaning |
|-------------|---------|
| `$80`       | game number, bit 7 = looping past level 20 |
| `$81`       | frame counter |
| `$82`       | random LFSR |
| `$84`       | joystick (SWCHA high nibble) or demo input |
| `$85-$90`   | pointers for score digits / object sprites (kernel scratch) |
| `$91/$92`   | hero gfx pointer, `$95/$96` hero colour pointer |
| `$93-$9A`   | P0 gfx/colour pointers used by the kernel |
| `$9B`       | hero x |
| `$9C`       | screen index in level **(verified)** |
| `$9D`/`$9E` | previous / furthest screen |
| `$9F`       | hero y |
| `$A0`       | special object x (ball) |
| `$A1-$A3`   | band object state |
| `$A4`       | level hue |
| `$A5-$A7`   | band object x (bottom, middle, top) **(verified)** |
| `$A8-$AA`   | second object x (tentacle, raft) |
| `$AB`       | power units left during bonus count |
| `$B0`       | hero facing (REFP1) |
| `$B2`       | dynamite left |
| `$B6`       | attract demo timer, 0 = game running |
| `$B7-$B9`   | score, BCD (`$A` = blank digit) |
| `$BE`       | PF0 mask |
| `$BF-$C1`   | PF1 bottom/middle/top **(verified)** |
| `$C2-$C4`   | PF2 bottom/middle/top **(verified)** |
| `$C5-$C7`   | band playfield colours |
| `$C8-$CA`   | band CTRLPF |
| `$E1`       | background colour (flash) |
| `$E6`       | kernel band counter (2 = top) |
| `$E9`       | vertical thrust / velocity |
| `$ED-$F1`   | saved band object state of the other screen |
| `$F2`       | level complete (miner reached) |
| `$F5`       | level, 0-based **(verified)** |
| `$F6`       | power used |

## Display kernel (bank 0)

- `$D00C`: coarse/fine horizontal positioning (`RESPx` + `HMPx`).
- `$D12C`: top cap, 5 lines.
- `$D17A-$D309`: one iteration per band, 3 bands:
  - write PF1, PF0, PF2, CTRLPF, band colour once per band
  - 19 lines with the first P0 object (`($93),y` gfx, `($97),y` colour)
  - reposition P0 mid band, 13 lines with the second P0 sprite (`($99),y`)
  - P1 (hero) and M1 (laser) drawn on every line from `($91),y`/`($95),y`
  - TIA collision latches stored per band (`$D2-$D7`, `$E3-$E5`, `$F4`)
- The game uses the TIA collision registers (P1 vs playfield, P0 vs P1, M1 vs
  playfield/P0) for walls, enemies and laser hits. The remake has to replace
  them with its own pixel or box tests.
- Layout from MAME screenshots: game area about 124 lines (bands roughly
  43/40/40 lines including the cap), HUD below (POWER bar, reserve lives,
  dynamite, score or "LEVEL: n"), Activision logo.

## Graphics

All sprite data is in bank 0, `$DA00-$DFFF`, stored upside down (the kernel
counts lines down). It holds: hero frames (propeller animation, walking,
standing), spider, moth, bat, snake, tentacle, miner, lamp, raft, dynamite,
explosion frames, digits 0-9, "POWER", "LEVEL:", copyright text and the
Activision logo, plus the colour tables (`$DB..`, `$DD..`). The exact pointer
to sprite mapping is open.

## Sound

Two TIA channels, about 60 lines of code (`$D58C-$D636`), no music:

- Channel 0 (`AUDV0` fixed 5 unless dying): propeller while the button is held
  (`$DB9F`/`$DBA3` AUDC/AUDF by frame), rescue/power count tones
  (`$DBA7`/`$DBAB`), death sound (`$DBA0`, volume from `$AD`).
- Channel 1: `AUDC1 = 8` (9-bit poly noise): explosions (`$DE-$E0` timers),
  power bonus count (`$AB`), walking/idle ticks (`$DBAF`).
- Silent during the attract demo.

## Open items

- Meaning of the `$FE00` bits 7-2 values (miner, raft, tentacle, lamp).
- Which sprite each object type shows; enemy animation tables `$FF8B-$FF9F`.
- Exact band heights and the y coordinates used by collision code.
- Hero physics: thrust (`$E9`), gravity, walk speed, landing.
- Laser range and the time it needs to break a wall (`$F7-$F9`).
- Lamp: kicking it out darkens the screen (only sprites stay visible).
- Power drain rate and the power bar (`$F6`, pointers `$FF8B-$FF9B`).

## Background

- [AtariAge H.E.R.O. maps by Adam Lewis](https://atariage.com/2600/archives/HEROMaps/index.html):
  maps of all 20 levels, useful to verify the room decoder.
- [AtariAge manual](https://atariage.com/manual_html_page.php?SoftwareLabelID=228):
  scoring, lives, game variations.
- [Hardcore Gaming 101](http://www.hardcoregaming101.net/hero/): gameplay
  overview; its "levels 13-20 loop" matches the code.
- [Wikipedia](https://en.wikipedia.org/wiki/H.E.R.O._(video_game)).

## Tools used

In `romconv/internal/hero/tools/`:

- `dis6502.py`: recursive 6502 disassembler for F8 carts (follows bank
  switches through hotspot accesses, origins `$D000`/`$F000`).
- `harness.lua`, `run_mame.sh`: headless MAME `a2600` with scripted input,
  RAM pokes, RAM dumps, bank aware ROM read log, snapshots.
- `capture_rooms.py`: snapshots of all 256 rooms (pokes `$F5`, `$9C`, `$A0`).
- `roomcheck.py`: decode check against the snapshots, writes the host test
  fixture `source/test/test_hero_room/hero_mame_bands.h`.
- `gfxdump.py`: ROM bytes as bitmap columns, to find sprites.
- `make_logo.sh`: `logos/hero.png` from a level 1 snapshot.
