# Video vsync and vblank

Notes for the case we need tear-free output later. A working scanline-polling
version existed and was dropped because the vanguard glitches were fixed by
snapshotting video state (`seqlock.h`). The code is kept below. Technique 1 is
applied on ST7789.

Observed (vanguard):

- ST7789, 80 MHz, no vsync: visible tear line, moving from top to bottom.
- ST7789, 80 MHz, technique 1: moving tear line gone, minor short tears
  remain.
- ILI9341, 40 MHz, no vsync: smooth.

## Terms

- **vblank**: the emulated machine's vertical blank. `run_frame()` raises the
  game CPU's vblank IRQ once per emulated frame; `publish()` snapshots video
  state right before it. Game timing only, unrelated to the panel.
- **vsync**: starting our SPI writes in step with the physical panel's refresh
  scan. Not implemented; this document covers how it could be.

## Two different problems

1. **Mixed emulation frames.** `prepare_frame()` races `run_frame()` on the
   other core: sprites, scroll and tile RAM come from different emulated frames.
   The result looks like wobble, edge garbage or flicker. Fix: `seqlock.h`
   snapshot at vblank. It does not involve the display at all.
2. **Tearing.** The panel shows part of the old and part of the new frame in
   one refresh, split at a horizontal line. It comes from the phase between
   our SPI writes and the panel's own refresh. Only this problem needs vsync.

Check which one you have before touching the display code. Problem 1 shows up
as wrong content, problem 2 as a straight split line, typically moving slowly.

## How the display works

The ST7789 / ILI9341 controller holds one full 240x320 frame in its own GRAM.
Two independent processes touch it:

- **Writer (us).** SPI commands `CASET`/`RASET` set a window, `RAMWR` (0x2C)
  streams pixels into it. The address counter advances per pixel, wraps to the
  next row at the window edge. MADCTL (0x36) decides the mapping from that
  counter to GRAM: `MY` (0x80) row order, `MX` (0x40) column order, `MV`
  (0x20) row/column exchange. See `video.md` for the bit table.
- **Scan (panel).** The controller refreshes the LCD from GRAM line by line,
  driven by its internal oscillator, independent of SPI. MADCTL `ML` (0x10)
  sets the refresh direction (0 = top to bottom). There is no double buffer:
  the scan shows whatever GRAM holds when it passes a line.

Refresh rate:

- ST7789: our init keeps the defaults, `FRCTRL2` (0xC6) 0x0F and porches
  (`PORCTRL`, 0xB2) of 12 + 12 lines: 344 lines per refresh, about 60 Hz,
  ~20 lines/ms.
- ILI9341: our init sends `FRMCTR1` (0xB1) `0x00, 0x10`: about 119 Hz.
- The oscillator has a tolerance of a few percent. Every unit differs.

How galagino writes a frame (`main.cpp`, `updateAudioVideo()`):

- Window 224 or 240 columns x 288 rows, centered (`setViewport`).
- 36 strips of 8 rows. Each is rendered into `frame_buffer`, copied to the DMA
  buffer and sent while the next strip renders.
- 80 MHz SPI: 240 x 288 x 16 bit = 1.1 Mbit, at least 13.8 ms (~21 rows/ms).
  Slow strip renders add gaps. Fits one refresh (16.7 ms): full rate (60 Hz).
- `TFT_SPICLK` below 80 MHz (`VIDEO_HALF_RATE`): at 40 MHz about 27.6 ms, sent
  in two halves, 30 Hz.
- Pacing: after the frame, `vTaskDelay(16 - t1)`. FreeRTOS tick is 1 ms, so
  frames come about every 16 ms (62.5 Hz) while the panel refreshes at about
  60 Hz. The phase drifts about 0.7 ms per frame and cycles every ~25 frames.

## Why it tears

Plot line against time. The scan is a sawtooth, the write a ramp of nearly the
same slope at 80 MHz:

```text
line
 320 |      /      /      /        scan (panel), ~20 lines/ms
     |     /      /     ,/
     |    /      /    ,' /         write (us), ~21 lines/ms at 80 MHz
     |   /      /   ,'  /
     |  /      /  ,'   /
     | /      / ,'    /
   0 |/      /,'     /
     +--------------------- time
              ^ write starts behind the scan, catches it: tear at that line
```

A refresh shows a clean frame if, during that pass, the scan never meets the
writer. Two cases:

- **Opposite directions** (write bottom-up, scan top-down): the two ramps
  always cross once per frame. Tear every frame, at a phase-dependent line.
  This was the ST7789 default without `TFT_VFLIP`: `MADCTL_DEFAULT` 0xC0
  (MY=1, write bottom to top) with ML=0 (scan top to bottom).
- **Same direction:** both run at about 20 lines/ms, so they only meet if the
  write starts close to the scan. The gap changes by 1-2 lines/ms over ~13 ms,
  so 10-30 lines: a few percent of all phases. Outside that window the pass
  shows either the whole old or the whole new frame.

Because of the drift above, the phase sweeps through that window regularly: a
tear on a frame or two out of ~25, instead of every frame.

## Technique 1: align scan direction (MADCTL_SCAN)

Make the panel refresh in the direction we write: set `ML` equal to `MY`.
In use on ST7789 (`video.cpp`).

The code, four places:

```c++
// ILI9341 branch of the MADCTL_DEFAULT block
#define MADCTL_SCAN(m) (m)

// ST7789 branch
#define MADCTL_SCAN(m) (((m) & 0x80) ? ((m) | 0x10) : ((m) & ~0x10))

// ST7789 init table
0x36, 1, MADCTL_SCAN(MADCTL_DEFAULT),

// Video::flip()
write8(MADCTL_SCAN(madctl));
```

Line by line:

1. ILI9341: shared code calls `MADCTL_SCAN`, so it must exist; identity keeps
   ILI9341 unchanged. Its default `TFT_MAC` 0x48 has `MY` = `ML` = 0, aligned.
   `TFT_VFLIP` or a cocktail flip sets `MY` with `ML` = 0; irrelevant at
   40 MHz (see below).
2. ST7789: `(m) & 0x80` tests `MY` (row write order, 1 = bottom to top). If
   set, `| 0x10` sets `ML` (panel refreshes bottom to top), else `& ~0x10`
   clears it (top to bottom). All other bits pass through. Default 0xC0
   becomes 0xD0, `TFT_VFLIP` 0x00 stays 0x00.
3. Init table: MADCTL (0x36) is sent aligned at boot. Before the fix it sent
   0xC0: write bottom to top, scan top to bottom.
4. `flip()`: cocktail flip XORs 0xC0 (`MY` and `MX`), which reverses the write
   order, so `ML` must follow. `madctl_last` keeps the value without
   `MADCTL_SCAN`, so the "unchanged" early return still compares like values.

- Cost: none at runtime. One bit.
- Gain: tear only in a window of a few percent of phases instead of every
  frame. Not a real vsync, the phase is still unknown: a short tear on a frame
  or two out of ~25 remains possible.
- Tested (vanguard, ST7789 80 MHz): the moving tear line is gone, minor
  short tears remain.
- Only helps when write and scan speed are close (80 MHz, full rate). At
  40 MHz the scan passes the writer during every write, in any direction.
- Not usable with `MV` set (`TFT_MAC 0x20`, some ILI9341 CYDs): written rows
  then run across the panel's scan lines, no refresh order matches the write.
- Prerequisite for techniques 2 and 3.

## Technique 2: poll the scanline (GSCAN)

`GSCAN` (0x45, same on ST7789 and ILI9341) returns the line currently scanned.
Start each frame right after the scan wraps; with aligned direction and a
writer at least as fast as the scan, the write stays ahead of the scan for the
whole frame. At 80 MHz the margin is thin (~21 vs ~20 lines/ms, plus the
16-row `TFT_Y_OFFSET` lead): slow strip renders can use it up.

Requirements:

- MISO (panel SDO) wired. On a board without it the line floats and returns
  random values; see pitfalls.
- A second SPI device on the same bus at low clock: panel reads are slow
  (ST7789 read cycle 150 ns). 1 MHz works.
- Reads end `RAMWR`. Send `RAMWR` again before the next pixel data; the window
  restarts at its origin, which is what a new frame wants anyway.

Read device, in `Video::begin()` next to the write device:

```c++
spi_device_interface_config_t read_cfg = if_cfg;
read_cfg.clock_speed_hz = 1000000;   // panel reads are slow
spi_bus_add_device(TFT_SPI_HOST, &read_cfg, &read_handle);
```

Read: one dummy clock, then 10 bits of line, MSB first:

```c++
uint16_t Video::readScanline(void) {
  spi_transaction_ext_t t = {};
  t.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_DUMMY | SPI_TRANS_USE_RXDATA;
  t.base.cmd = 0x45;   // GSCAN
  t.base.rxlength = 16;
  t.command_bits = 8;
  t.dummy_bits = 1;

  digitalWrite(TFT_DC, LOW);
  spi_device_polling_transmit(read_handle, &t.base);
  digitalWrite(TFT_DC, HIGH);
  return ((t.base.rx_data[0] << 8) | t.base.rx_data[1]) & 0x3ff;
}
```

Wait for the wrap. Called in the full-rate path once the first strip is
rendered, before it is written (`renderRow(0)`, `waitVSync()`,
`video.write()`):

```c++
#define VSYNC_TIMEOUT_US 25000 // > 1 refresh: no wrap seen, reads don't work
#define VSYNC_LATE_LINES 160   // about half a refresh

void Video::waitVSync(void) {
  if(!vsync || !read_handle) return;

  if(dma_active) {
    spi_device_get_trans_result(handle, &r_trans, portMAX_DELAY);
    dma_active = 0;
  }

  // Past half a refresh: wait for the wrap. Before it the frame is late
  // (took about one refresh, zaxxon): start at once, may tear, instead of
  // dropping to 30 Hz. The same sample seeds waitWrap, else a wrap between
  // two reads is missed.
  const uint16_t line = readScanline();
  if(line >= VSYNC_LATE_LINES) waitWrap(line);

  writeCommand(CMD_RAMWR);   // the reads ended RAMWR
}

void Video::waitWrap(uint16_t prev) {
  const uint32_t t0 = micros();
  for(;;) {
    // a read during a counter step can return a line lower than the one
    // before: only a big drop is the wrap
    const uint16_t line = readScanline();
    if(line + VSYNC_LATE_LINES < prev) return;
    prev = line;

    if(micros() - t0 > VSYNC_TIMEOUT_US) {
      vsync = false;   // no wrap seen: reads don't work, give up
      return;
    }
  }
}
```

Pacing: with vsync active, **drop the `vTaskDelay(16 - t1)` pad** in the
full-rate path. Two clocks (tick-based pad at 62.5 Hz, panel at ~60 Hz) drift
against each other and need extra heuristics to recover (an earlier version
had a measured refresh period and a "frame is due" rule for exactly that).
With the pad gone the panel is the only clock. Keep the pad as fallback when
vsync is disabled. Keep some `vTaskDelay(1)` so the idle task on the video core
gets CPU time within the task watchdog timeout.

Costs and pitfalls:

- Busy-wait on the video core, up to ~3 ms per frame (refresh minus write).
- Game speed follows the panel oscillator (a few percent off 60 Hz per unit).
  Audio is refilled per frame against a fixed 24 kHz I2S clock; check for
  underruns or overruns if the rate is off.
- Frames slower than one refresh (zaxxon, ~16-17 ms) cannot be synced: they
  start late and tear. The late-lines rule avoids halving their rate.
- Wrap detection by "drop larger than 160 lines" accepts noise: floating MISO
  gives random values, a big random drop counts as a wrap, vsync stays on and
  starts frames at random phase. Only a constant value hits the timeout.
  A plausibility check (line within the panel's range, several monotonic
  samples before the drop) would catch that.
- Useless with `MV` set (see technique 1).

### Why full rate only

A tear appears when scan and writer pass each other within one refresh.

| SPI clock | write 288 rows          | panel refresh (ST7789)  |
| --------- | ----------------------- | ----------------------- |
| 80 MHz    | ~13.8 ms (~21 rows/ms)  | ~16.7 ms (~20 lines/ms) |
| 40 MHz    | ~27.6 ms (~10 rows/ms)  | ~16.7 ms (~20 lines/ms) |

- 80 MHz: started at the wrap, the writer stays ahead of the scan for the
  whole frame (thin margin, see above). One refresh shows the old frame, the
  next the new one.
- 40 MHz: the write takes longer than one refresh, the scan overtakes the
  writer mid-frame. A timing model gives a split in about half (ST7789) to two
  thirds (ILI9341) of refreshes, yet ILI9341 at 40 MHz looks smooth
  (observed). Likely masked by the 30 Hz judder (unproven).

Half rate can still be tear free over two refreshes. Start at the wrap in scan
direction: during the first pass the scan stays ahead of the writer and shows
the old frame entirely. The second pass starts at 16.7 ms and would catch the
writer only at ~36 ms, the write ends at 27.6 ms: the new frame entirely.
Requires:

- Locking the 30 Hz frame to every second wrap.
- Sending both halves without a gap that pushes the total past two refreshes
  (33.3 ms). The half-rate path pads to 16 ms between the halves today.
- A panel refresh near 60 Hz. The ILI9341 init runs at about 119 Hz, so the
  scan passes the writer several times per write.

More work in the main loop and panel setup than a `waitVSync()` call, and
ILI9341 at 40 MHz already looked smooth (vanguard). Not done: `waitVSync()` is
a no-op at half rate.

### In the frame loop

Three clocks are involved:

| clock          | source                  | without vsync      | with vsync    |
| -------------- | ----------------------- | ------------------ | ------------- |
| FreeRTOS tick  | pad `vTaskDelay`        | master, ~62.5 Hz   | fallback only |
| panel scan     | panel oscillator        | ignored, drifts    | master        |
| machine vblank | IRQ in `run_frame()`    | follows the notify | follows panel |

Cores and handoff (full rate, `updateAudioVideo()` in `main.cpp`,
`emulation_task()` in `emulation.cpp`):

```text
 video core (Arduino loop)                  emulation core (emulation_task)
 ------------------------------------       ------------------------------
 prepare_frame()   seqlock read of          ulTaskNotifyTake() blocked
                   last published state            |
 renderRow(0)      strip 0 in frame_buffer         |
 waitVSync()       flush DMA, GSCAN,               |
                   busy-wait for wrap              |
 write strips 0..35, audio.transmit() x6           |
 emulation_videoRendered()                         |
 [vTaskDelay(1) sometimes, idle watchdog]          |
 emulation_notifyGive() ---------------------> run_frame(): CPUs 1 frame,
                                               vblank.publish(), vblank IRQ
 loop: prepare_frame() ...                     wdt reset, block again
```

- One notify per video loop. Counting semaphore: exactly one `run_frame()`
  per loop, backlog is caught up.
- `run_frame(N+1)` runs on the other core while the video core sends frame N.
  `prepare_frame()` right after the notify reads the snapshot published by
  `run_frame(N)`.
- The seqlock decouples content from timing: render sees one emulated vblank's
  state whatever the emulation core is doing. No locks, no blocking.

Machine vblank and panel vsync solve the two problems from the start of this
document:

1. Machine vblank (`publish()` right before the vblank IRQ): consistent
   content. Lives entirely in the emulation core's timeline, knows nothing
   about the panel.
2. Panel vsync (`waitVSync()`): when pixels land in GRAM relative to the scan.
   With aligned direction (technique 1) and a writer as fast as the scan: no
   tear.

They couple only through the notify. Vsync sets the video loop period, the
loop gives one notify per period, one notify is one emulated frame and one
machine vblank:

- Frequency: machine vblank rate = panel refresh rate. The game runs at the
  panel oscillator's speed instead of 62.5 Hz.
- Phase: irrelevant. Where the emulated vblank falls relative to the panel
  wrap is absorbed by the seqlock snapshot.
- Latency: unchanged, about one frame from `publish()` to pixels.

Timing budget at 80 MHz:

```text
|<------------- refresh ~16.7 ms ------------->|
 wrap                                          wrap
 | write 288 rows ~13.8 ms  | slack ~2.9 ms    |
                            | notify, prepare_frame, renderRow(0),
                            | GSCAN busy-wait
```

The idle watchdog `vTaskDelay(1)` fits into the slack; an occasional yield
does not miss the wrap.

## Technique 3: TE pin

The controller pulses its TE output when the scan reaches a set line: `TEON`
(0x35) enables it, `STE` (0x44) sets the line. Wire TE to a GPIO, take the edge
as an interrupt, give a semaphore, block the video task on it before the first
strip.

- No SPI reads, no busy-wait: the video task sleeps until the pulse.
- No noise problem, no MISO needed.
- Needs a free GPIO and a wire. Neither the CYD nor the DevKit wiring in
  `config.h` connects TE. Hardware change, so only for custom builds.
- Same pacing rules as technique 2 (panel is the clock, drop the pad).

## Frame rate: 62.5 Hz instead of 60 Hz

The full-rate loop pads each frame to 16 ms with a 1 ms tick: 1000 / 16 =
62.5 Hz. The emulation runs one frame per loop, so games run about 4% fast.
Half rate pads two frames to 33 ms: 60.6 Hz.

Possible fix, deadline pacing in `updateAudioVideo()`: delays alternate
16/17/17 ms, 60.0 Hz on average. Replaces the pad:

```c++
static uint32_t next_us = micros();
next_us += 16667;                                   // 60 Hz
const int32_t wait_ms = (int32_t)(next_us - micros()) / 1000;
if(wait_ms > 0) { vTaskDelay(wait_ms); last_yield_ms = millis(); }
else if(wait_ms < -50) next_us = micros();          // stall: resync, no catch-up burst
```

- Tearing unchanged: the timing model gives 1.7% torn refreshes at 60 Hz vs
  1.8% at 62.5 Hz (ST7789, 80 MHz, technique 1). Slower drift against the
  panel: tears come less often but last more frames.
- With technique 2 the panel sets the rate instead.

## Things that do not help

- **Changing the panel frame rate** (0xC6 / 0xB1): changes the drift speed,
  not the phase. Tear line moves slower, stays longer in one place.
- **Double buffering in GRAM:** GRAM is exactly one 240x320 frame, no second
  page. Vertical scroll (0x33/0x37) cannot flip between buffers.
- **A full framebuffer on the ESP32:** does not change when the panel scans,
  and costs 138 KB of RAM we do not have.

## Recommendation

1. Fix content first (`seqlock.h`). Most visible "tearing" in scrolling games
   was mixed emulation state.
2. If a real split line remains: technique 1 (done on ST7789).
3. Still visible: technique 2 on boards with MISO wired, with the pad removed.
4. Custom hardware: technique 3.
