# Video vsync and vblank

Notes for the case we need tear-free output later. A working scanline-polling
version existed and was dropped because the vanguard glitches were fixed by
snapshotting video state (`seqlock.h`). The code is kept below.

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

The ST7789 / ILI9341 controller holds a full frame in its own GRAM
(240x320x16 bit). Two independent processes touch it:

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

- ST7789: `FRCTRL2` (0xC6), default 0x0F, about 60 Hz. Porches (`PORCTRL`,
  0xB2) add lines, so the scan counts somewhat more than 320 lines per refresh.
- ILI9341: `FRMCTR1` (0xB1). Our init sends `0x00, 0x10` (see datasheet table
  for the resulting rate, well above 60 Hz).
- The oscillator has a tolerance of a few percent. Every unit differs.

How galagino writes a frame (`main.cpp`, `updateAudioVideo()`):

- Window 224 or 240 columns x 288 rows, centered (`setViewport`).
- 36 strips of 8 rows, each rendered into `frame_buffer` and sent by DMA.
- 80 MHz SPI: 240 x 288 x 16 bit = 1.1 Mbit, about 13.8 ms plus gaps. Faster
  than one refresh (about 16.7 ms), so full rate (60 Hz).
- 40 MHz SPI (`VIDEO_HALF_RATE`, all ILI9341 envs): about 27.6 ms, sent in two
  halves, 30 Hz.
- Pacing: after the frame, `vTaskDelay(16 - t1)`. FreeRTOS tick is 1 ms, so
  frames come about every 16 ms (62.5 Hz) while the panel refreshes at about
  60 Hz. The phase drifts about 0.7 ms per frame and cycles every ~25 frames.

## Why it tears

Plot line against time. The scan is a sawtooth, the write a steeper ramp:

```text
line
 320 |      /      /      /        scan (panel), ~19 lines/ms
     |     /      /     ,/
     |    /      /    ,' /         write (us), ~22 lines/ms at 80 MHz
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
  This is the default on ST7789: `MADCTL_DEFAULT` is 0xC0 (MY=1, write
  bottom to top) with ML=0 (scan top to bottom).
- **Same direction, writer faster:** the writer only meets the scan if it
  starts just behind it and catches up before the frame ends. At 80 MHz the
  gap closes at about 3 lines/ms over ~13 ms, so about 40 lines, roughly 12%
  of all phases. Outside that window the pass shows either the whole old or the
  whole new frame.

Because of the drift above, the phase sweeps through that window regularly: a
tear line on a few frames out of ~25, instead of every frame.

## Technique 1: align scan direction (MADCTL_SCAN)

Make the panel refresh in the direction we write: set `ML` equal to `MY`.

```c++
#define MADCTL_MY 0x80
#define MADCTL_ML 0x10
#define MADCTL_SCAN(m) (((m) & MADCTL_MY) ? ((m) | MADCTL_ML) : ((m) & ~MADCTL_ML))
```

Apply it everywhere MADCTL is sent: both init tables (`0x36, 1,
MADCTL_SCAN(MADCTL_DEFAULT)`) and `Video::flip()` (`write8(MADCTL_SCAN(madctl))`),
because cocktail flip toggles `MY` and `ML` must follow.

- Cost: none at runtime. One bit.
- Gain: tear only in the ~12% phase window instead of every frame.
- Does not work with `MV` set (`TFT_MAC 0x20`, some CYDs): written rows then
  run across the panel's gate lines, no refresh order matches the write.
- Changes the refresh order on ILI9341 half-rate panels too. Only useful there
  combined with technique 2 or 3.
- Prerequisite for techniques 2 and 3.

## Technique 2: poll the scanline (GSCAN)

`GSCAN` (0x45, same on ST7789 and ILI9341) returns the line currently scanned.
Start each frame right after the scan wraps; with aligned direction and a
faster writer, the write stays ahead of the scan for the whole frame.

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

Wait for the wrap. Called in the full-rate path once the first strip is rendered, before
it is written (`renderRow(0)`, `waitVSync()`, `video.write()`):

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

- Busy-wait on the video core, up to ~3.6 ms per frame (16.7 - 13 ms).
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

Half rate (40 MHz): a writer slower than the scan can still be tear free.
Start at the wrap in scan direction: during the first pass the scan stays
ahead of the writer and shows the old frame entirely; the second pass shows
the new one if the write finishes within two refreshes (27.6 ms < 33.3 ms).
Requires locking the 30 Hz frame to every second wrap and sending both halves
without a gap that pushes the total past two refreshes. Not implemented.

## Technique 3: TE pin

The controller can signal vblank on its TE output: `TEON` (0x35) enables it,
`STE` (0x44) sets the line where it fires. Wire TE to a GPIO, take the edge as
an interrupt, give a semaphore, block the video task on it before the first
strip.

- No SPI reads, no busy-wait: the video task sleeps until vblank.
- No noise problem, no MISO needed.
- Needs a free GPIO and a wire. Neither the CYD nor the DevKit wiring in
  `config.h` connects TE. Hardware change, so only for custom builds.
- Same pacing rules as technique 2 (panel is the clock, drop the pad).

## Things that do not help

- **Changing the panel frame rate** (0xC6 / 0xB1): reduces drift, cannot fix
  phase. Tear line moves slower, stays longer in one place.
- **Double buffering in GRAM:** GRAM is exactly one 240x320 frame, no second
  page. Vertical scroll (0x33/0x37) cannot flip between buffers.
- **A full framebuffer on the ESP32:** does not change when the panel scans,
  and costs 138 KB of RAM we do not have.

## Recommendation

1. Fix content first (`seqlock.h`). Most visible "tearing" in scrolling games
   was mixed emulation state.
2. If a real split line remains and bothers: technique 1 alone.
3. Still visible: technique 2 on boards with MISO wired, with the pad removed.
4. Custom hardware: technique 3.
