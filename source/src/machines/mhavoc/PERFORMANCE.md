# Major Havoc performance

Major Havoc ran below 60 Hz on the ESP32, worst in the remote (WiFi) build: under 40 Hz in
gameplay. This file records what was optimized, what was tried and rejected, and where it ended.

## Workload

One emulated frame (60 Hz) needs:

| Part | Clock | Cycles per frame | Notes |
|---|---|---|---|
| alpha 6502 (game) | 2.5 MHz | 41667 | game logic and 3D math in 8 bit software |
| gamma 6502 (sound) | 1.25 MHz | 20833 | sound driver for 4 POKEYs, inputs, EEPROM |
| AVG | | | vector list decode on every GO |

Compared with Star Wars (two 6809 at 1.5 MHz), mhavoc has more emulated cycles. It also has no
math box, so its multiplies and shifts run as 6502 code. In the remote build, the 6502 core runs
from flash (`M6502_NO_IRAM`, WiFi needs the IRAM), and every 6502 byte access goes through the
`alpha_read`/`alpha_write` callbacks: about 1.1-1.4 us per instruction.

The two CPUs run interleaved in slices of 128 alpha cycles (`SLICE`). After each slice, gamma
catches up and the timer ticks raise IRQs. Alpha gets inputs, the roller and random numbers from
gamma by synchronous RPC: send a command (9176), poll for the reply (917a); after 128 polls
without a reply, alpha resets gamma.

## Done

1. **Per-game IRAM code moved to flash in remote builds** (`GAME_NO_IRAM`). That frees IRAM
   for the m6809 core and cuts flash cache conflicts for all games. Attract mode went from
   47 to 51 Hz.
2. **Idle-loop skips**, charged in exact cycles:
   - gamma 802c: main loop with empty queues
   - alpha 821c: waits for the IRQ flag
   - alpha 91ca: delay loop after gamma commands
   - alpha 917a: polls for the gamma reply, counting toward its timeout
3. **Native alpha routines** (`native_alpha`). When the alpha PC reaches the entry of a hot
   routine, C++ runs the whole routine, including its RTS. It produces the same registers, flags,
   memory and cycle count as the interpreter. A routine does not run natively when an IRQ is due
   or the D flag is set. A randomized test compares every routine against the interpreter
   (`test_mhavoc_native`).
   - Leaves:
     - 995d/9959/9970/9965: AVG word writer and pointer advance
     - 9979/997b/997d/9998: AVG vector writer
     - c6ec: arithmetic shift right
     - 949f: table multiply
   - Object routines that call those leaves:
     - b434/b448: object scaling
     - b628: drops an out-of-range object
     - b655: draws an object
   - Cost of the approach: a routine runs as one step, so a slice can overrun by its length,
     and a tick and its IRQ can come up to ~350 alpha cycles late.
4. **Trap-page filter.** `skip_alpha` used to compare the PC against every native entry and idle
   loop before each alpha instruction. A bitmap of the pages that hold them now rejects all other
   PCs with one lookup (`trap_page`). `alpha_read` checks RAM right after ROM, its two most
   frequent targets.

## Rejected

| Idea | Result |
|---|---|
| 6502 direct-pointer fast path in the shared core (`code[]`/`zp`, NULL test per access) | mhavoc -2% in remote, other 6502 games +15-30%; reverted |
| m6502 core in IRAM | no gain (local: 1100 vs 1111 ns per instruction); remote IRAM too small for its 10.4 KB |
| `-O2` for `m6502.c` | -3% per instruction; within the flash layout noise, not kept |
| mhavoc handlers in IRAM | -2-3% per instruction; not worth the scarce remote IRAM |
| Porting the gamma sound driver (8672/8355, 48% of gamma) | ~1.8 ms, but a whole driver with jump tables; too large to keep exact |
| More idle loops (AVG done, clock polling) | none exist; the rest is real work |

## Open leads

- **Alpha routine d72d/d74c.** About 950 instructions on heavy frames, many branches.
- **Coin IRQ 97fa.** 456 instructions per frame. It reads IN0, so the native version would need
  the exact cycle of each read.
- **Gamma on the video core**, like the gyruss audio task. It could save ~3.8 ms on the
  emulation core, but:
  - The video core has only ~5 ms free per frame, and gamma needs up to 7.4 ms on peak frames.
  - The alpha-gamma RPC would have to wait across cores.
- **AVG decode on the video core.** It could save ~1.4 ms, with the same budget problem.
- **Direct-access 6502 core for alpha only**: a second copy of `m6502.c` built with
  `M6502_DIRECT`, so other games keep their core. The local build measured -22% per instruction;
  the remote gain is unknown.
- **AVG decode per variant.** `avg::decode` branches on the game variant at every step.

## Results

Device: azDelivery, remote build. Gameplay measured over 100 s with the autoplay rig
(`MHAVOC_PROFILE`, `MHAVOC_AUTOPLAY`). The budget is 16.7 ms per frame.

- **Cpu** is the DEBUG_TIMING emulation time per frame: average and p90 over 10-frame windows.
- **< 60 Hz** is the share of 10-frame windows over 170 ms.
- **Alpha** is the alpha time per frame from the profile; **ns/instr** is alpha time per
  interpreted alpha instruction.

| Build | Cpu avg | Cpu p90 | < 60 Hz | Alpha | Alpha instr/frame | ns/instr |
|---|---|---|---|---|---|---|
| IRAM to flash, idle skips | 14.3 ms | 19.4 | 48% | 10.1 ms | 8972 | 1129 |
| + native leaves | 12.5 ms | 17.6 | 15% | 8.0 ms | 5676 | 1411 |
| + trap-page filter | 11.5 ms | 16.1 | 3% | 6.9 ms | 5749 | 1201 |

- **Before any of this.** Emulation took 22.2 ms per frame in gameplay (max 27.9) and ran
  below 45 Hz 56% of the time. That was measured with an older method, so it is not part of
  the table.
- **Last row.** Per frame: alpha 6.9 ms, gamma 3.8 ms, AVG decode 1.5 ms.
- **Native leaves row.** Alpha instructions fell by a third, but ns per instruction rose 25%.
  The flash layout changed with the new code (see Measuring), so part of the gain was lost to
  that.
- **Object natives** (b434/b448/b628/b655). On a scripted host run, they cut alpha instructions
  per frame by 18% and frames above 7k instructions from 1638 to 144 (of 3600). On the device
  autoplay path they fire only 2-4 times a frame.

## Measuring

- **Device runs are deterministic.** Repeat logs of one image differ by about 1 us.
- **Flash layout swings speed** by up to 10% between builds of the same code (once 2x after
  adding a single printf). Compare ns per instruction, not milliseconds alone.
- **Native routines change the game path.** They shift IRQ timing, so the game diverges
  (another level, more objects). Instructions per frame are only comparable within one
  build. Across builds, compare cost per instruction or per busy cycle.
- **Host replay matches the device.** Running the autoplay input from reset on the host
  reproduces the device path exactly. Profile there per subroutine (shadow call stack
  profiler).
