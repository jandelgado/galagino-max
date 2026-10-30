// HSTX DVI scanout. Timing, command lists and the ping-pong DMA follow
// pico-examples hstx/dvi_out_hstx_encoder (BSD-3-Clause, Raspberry Pi); the
// pin mapping loop follows pico-galagino's pico_hdmi/video_output.c (GPLv3).
#ifdef HAL_RP2350
#include "../../config.h"  // the board profile selects the backend
#ifdef HAL_VIDEO_DVI_HSTX
#include "video_dvi_hstx.h"
#include "../video_width.h"
#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/structs/bus_ctrl.h>
#include <hardware/structs/hstx_ctrl.h>
#include <hardware/structs/hstx_fifo.h>
#include <hardware/structs/nvic.h>
#include <hardware/sync.h>
#include <pico/time.h>
#include "../hal_attr.h"
#include "hdmi_line.h"
#include "rtos.h"
#ifdef HAL_AUDIO_DVI
#include "audio_dvi.h"
#endif

namespace hal {

namespace {

using namespace hstx;

// 640x480 60 Hz (CEA-861 VIC 1): 25.2 MHz pixel clock, 10 bits per pixel
// and lane, two bits per HSTX clock: clk_hstx = 126 MHz.
const uint32_t HSTX_HZ = 126000000;
const uint32_t V_FRONT_PORCH = 10;
const uint32_t V_SYNC_WIDTH = 2;
const uint32_t V_BACK_PORCH = 33;
const uint32_t V_ACTIVE = dvi::OUT_LINES;
const uint32_t V_BLANK = V_FRONT_PORCH + V_SYNC_WIDTH + V_BACK_PORCH;
const uint32_t V_TOTAL = V_BLANK + V_ACTIVE;
const uint32_t HSTX_FIRST_PIN = 12;

// Command lists, padded with NOPs to at least the HSTX FIFO size so the DMA
// does not ping-pong too fast for the IRQ.
uint32_t vblank_vsync_off[] = {
  CMD_RAW_REPEAT | H_FRONT_PORCH, SYNC_V1_H1,
  CMD_RAW_REPEAT | H_SYNC_WIDTH, SYNC_V1_H0,
  CMD_RAW_REPEAT | (H_BACK_PORCH + H_ACTIVE), SYNC_V1_H1,
  CMD_NOP
};
uint32_t vblank_vsync_on[] = {
  CMD_RAW_REPEAT | H_FRONT_PORCH, SYNC_V0_H1,
  CMD_RAW_REPEAT | H_SYNC_WIDTH, SYNC_V0_H0,
  CMD_RAW_REPEAT | (H_BACK_PORCH + H_ACTIVE), SYNC_V0_H1,
  CMD_NOP
};
#ifndef HAL_AUDIO_DVI
uint32_t vactive_line[] = {
  CMD_RAW_REPEAT | H_FRONT_PORCH, SYNC_V1_H1, CMD_NOP,
  CMD_RAW_REPEAT | H_SYNC_WIDTH, SYNC_V1_H0, CMD_NOP,
  CMD_RAW_REPEAT | H_BACK_PORCH, SYNC_V1_H1,
  CMD_TMDS | H_ACTIVE
};
#endif

// Picture placement: 288 source rows, 1x or scaled (dvi_line.h)
const uint32_t SRC_ROWS = 36 * 8;
const uint32_t PIC_LINES = DVI_SCALE == 1 ? SRC_ROWS : dvi::scaledLines(SRC_ROWS);
const uint32_t PIC_TOP = (V_ACTIVE - PIC_LINES) / 2;
const uint32_t LINE_WORDS = H_ACTIVE / 2;
// The IRQ requeues a channel while the other one sends the next transfer,
// so every transfer must take long enough to send. An active line is one
// transfer (sync/porch commands, then the pixels); a short command list
// right after pixels would leave well under 1 us, so the last active line
// carries the first blank line as its tail.
#ifdef HAL_AUDIO_DVI
// HDMI: every line carries a data island (hdmi_line.h)
const uint32_t HEADER_WORDS = hdmi::ACTIVE_WORDS;
#else
const uint32_t HEADER_WORDS = count_of(vactive_line);
#endif
const uint32_t ACTIVE_WORDS = HEADER_WORDS + LINE_WORDS;
const uint32_t TAIL_WORDS = count_of(vblank_vsync_off);

// memset lives in flash: the IRQ clears with this loop from RAM
__attribute__((optimize("no-tree-loop-distribute-patterns")))
HAL_FAST_CODE void clearWords(uint32_t *words, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    words[i] = 0;
  }
}

// Source picture, RGB565 in SPI byte order (line builders swap), rows in
// scan order (flips applied).
const uint32_t SRC_WIDTH = DviHstxVideo::MAX_WIDTH;
uint16_t frame[SRC_ROWS][SRC_WIDTH];

// Native mode: `frame` holds the strip ring (dvi_line.h). slot_tag[slot] =
// the strip written there last; the IRQ sends a line only from its own
// strip, else black.
static_assert(dvi::RING_SLOTS * dvi::NATIVE_STRIP_ROWS * VIDEO_NATIVE_W <= SRC_ROWS * SRC_WIDTH,
              "native ring does not fit the framebuffer");
static_assert(dvi::NATIVE_ROWS == VIDEO_NATIVE_H, "native rows differ from the HAL raster");
// native_rows: lines built since the switch (IRQ). Wraps after ~41 h in one
// native session: rows misalign until the next resync, never stale pixels.
volatile uint32_t native_rows;
volatile uint32_t slot_tag[dvi::RING_SLOTS];
const uint32_t NO_STRIP = 0xffffffffu;
volatile uint32_t native_misses; // lines sent black: strip late
int8_t pending_mode = -1;        // switchMode: native_mode after the resync
uint16_t pending_width;          // setNative: native_width after the resync
#ifdef DEBUG_TIMING
uint32_t native_late;            // strips submitted after the beam built their first row
uint32_t native_wait_us;         // video core time spent waiting for a free slot
uint32_t native_frame_start_us, native_frame_max_us;
#endif

// The DMA IRQ has the highest priority: a late requeue makes a channel
// replay a stale buffer, the HSTX command stream loses its framing and stays
// garbled (about 160 frames/s, no picture). At that priority it may not call
// FreeRTOS, so it pends wake_irq, a low priority IRQ that gives the semaphore.
DviHstxVideo *self;
SemaphoreHandle_t wake;          // given as the beam reads strip 0 and the last row
int wake_irq;
volatile uint32_t scan_frames;   // vblanks so far

// The IRQs run on the emulation core: the line builds (~3 ms a frame) would
// take the video core's render time, the emulation core has time to spare.
// resync() runs on the video core: it masks the IRQ and waits until a
// handler running on the other core is done (both flags set before read).
SemaphoreHandle_t irqs_ready;
volatile bool irq_busy;
volatile bool resyncing;

// Scanout watchdog (endFrame, nativeStrip): no beam progress for STALL_MS, or
// more than MAX_FPS frames per second, means the DMA chain lost its framing.
const uint32_t STALL_MS = 100;
const uint32_t MAX_FPS = 75;
uint32_t rate_start_us;
uint32_t rate_start_frames;
uint32_t resyncs;

// Ping-pong line buffers: the IRQ builds line n + 1 while line n is sent.
uint32_t line_buf[2][ACTIVE_WORDS + TAIL_WORDS];
uint16_t built_width[2];
uint32_t black_line[2][ACTIVE_WORDS + TAIL_WORDS];

#ifdef HAL_AUDIO_DVI
// HDMI audio: 48 kHz, every 24 kHz sample twice, four per sample packet. The
// IRQ encodes a packet on every line it is due (200 a frame); vblank lines
// carry Audio Clock Regeneration and the InfoFrames. Precomputed islands per
// vsync level (hdmi::VSync).
const uint32_t AUDIO_HZ = 48000;
const uint32_t LINE_HZ = 25200000 / 800;
const uint32_t PACKET_SAMPLES = 4;
const uint32_t ACR_N = 6144;      // HDMI table for 48 kHz at 25.2 MHz
const uint32_t ACR_CTS = 25200;
const uint32_t AVI_LINE = 1;
const uint32_t AUDIO_INFO_LINE = 2;

uint32_t blank_buf[2][hdmi::BLANK_WORDS];
uint32_t null_island[2][hdmi::ISLAND_WORDS];
uint32_t acr_island[2][hdmi::ISLAND_WORDS];
uint32_t avi_island[hdmi::ISLAND_WORDS];
uint32_t audio_info_island[hdmi::ISLAND_WORDS];
// What an island slot holds: a null island is copied only when the slot
// held something else (most lines carry no packet).
enum Held : uint8_t { HELD_OTHER, HELD_NULL_OFF, HELD_NULL_PULSE };
Held held_line[2];
Held held_black[2];
Held held_blank[2];
uint32_t audio_due;     // samples x line rate
uint32_t cs_frame;      // IEC 60958 channel status frame
uint32_t underruns;
#endif

int ch_ping;
int ch_pong;
bool pong;
uint32_t v_scanline = 2;  // ping and pong are queued before the first IRQ

// The render follows the beam: it overwrites a strip only once the current
// pass has read it, the next pass shows it. Every row gets a whole frame
// between write and read, so a slow render never tears (rendering ahead of
// the beam from vblank tore from mid screen on, 1943 at 1x in nearly every
// heavy frame). Cost: one frame of latency.
//
// Beam position: pass number * PASS_SPAN + source rows read in that pass.
// PASS_SPAN is a power of two, so the position wraps cleanly at 2^32.
const uint32_t STRIP_ROWS = SRC_ROWS / 36;
const uint32_t PASS_SPAN = 512;
volatile uint32_t beam;
uint32_t pass_base;    // beam at the start of the current pass (IRQ)
uint32_t pass_rows;    // rows the current pass read (IRQ)
uint32_t render_base;  // pass_base of the pass the screen's render follows
uint32_t render_start_us;

HAL_FAST_CODE void signalWake() {
  nvic_hw->ispr[wake_irq / 32] = 1u << (wake_irq % 32);
}

void wakeIrq() {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(wake, &woken);
  portYIELD_FROM_ISR(woken);
}

// both channels queued with blank lines, as at start
void queueBlankLines() {
  dma_channel_hw_t *ping = &dma_hw->ch[ch_ping];
  dma_channel_hw_t *pongCh = &dma_hw->ch[ch_pong];
  ping->read_addr = (uintptr_t)vblank_vsync_off;
  ping->transfer_count = count_of(vblank_vsync_off);
  pongCh->read_addr = (uintptr_t)vblank_vsync_off;
  pongCh->transfer_count = count_of(vblank_vsync_off);
  pong = false;
  v_scanline = 2;
}

// EN low stops the HSTX but keeps the FIFO's words. Words left from an
// aborted line would start the new stream mid-command: the expander reads
// pixels as commands, the DMA runs about 2.3x fast for good. Drains the FIFO
// into the running HSTX with NOPs past the longest command (12-bit count)
// the expander may be in, so it stops on a command boundary.
void drainHstx() {
  const uint32_t MAX_COUNT = 4096, FIFO_WORDS = 8;
  const uint32_t NOPS = MAX_COUNT + FIFO_WORDS;
  const uint32_t TIMEOUT_US = 2000; // takes 0.35-0.6 ms
  const uint32_t start = time_us_32();
  uint32_t pushed = 0;
  while (pushed < NOPS || !(hstx_fifo_hw->stat & HSTX_FIFO_STAT_EMPTY_BITS)) {
    if (time_us_32() - start > TIMEOUT_US) {
#ifdef DEBUG_TIMING
      static uint32_t timeouts;
      timeouts++;
      printf("DVI: HSTX drain timeout %u\n", (unsigned)timeouts);
#endif
      break; // halted HSTX: no hang
    }
    if (pushed < NOPS && !(hstx_fifo_hw->stat & HSTX_FIFO_STAT_FULL_BITS)) {
      hstx_fifo_hw->fifo = CMD_NOP;
      pushed++;
    }
  }
}

#ifdef HAL_AUDIO_DVI
HAL_FAST_CODE void copyIsland(uint32_t *out, const uint32_t *island) {
  for (uint32_t i = 0; i < hdmi::ISLAND_WORDS; i++) {
    out[i] = island[i];
  }
}

// Data island of blank line `scanline` (0 .. V_BLANK - 1) or of an active
// line (V_BLANK and up): fixed packets on some vblank lines, else an audio
// packet when one is due, else a null packet.
HAL_FAST_CODE void writeIsland(uint32_t *out, Held &held, uint32_t scanline) {
  const bool vpulse = scanline >= V_FRONT_PORCH && scanline < V_FRONT_PORCH + V_SYNC_WIDTH;
  const hdmi::VSync v = vpulse ? hdmi::VSync::PULSE : hdmi::VSync::OFF;
  const bool backPorch = scanline >= V_FRONT_PORCH + V_SYNC_WIDTH && scanline < V_BLANK;
  const Held null = vpulse ? HELD_NULL_PULSE : HELD_NULL_OFF;
  const Held before = held;
  held = HELD_OTHER;
  if (scanline == V_FRONT_PORCH || (backPorch && scanline % 4 == 0)) {
    copyIsland(out, acr_island[vpulse]);
    return;
  }
  if (scanline == AVI_LINE) {
    copyIsland(out, avi_island);
    return;
  }
  if (scanline == AUDIO_INFO_LINE) {
    copyIsland(out, audio_info_island);
    return;
  }
  if (audio_due < PACKET_SAMPLES * LINE_HZ) {
    if (before != null) {
      copyIsland(out, null_island[vpulse]);
    }
    held = null;
    return;
  }

  audio_due -= PACKET_SAMPLES * LINE_HZ;
  int16_t s[2] = {0, 0};
  if (!hdmi::audioRing.pop(s, 2)) {
    underruns++;
  }
  const int16_t samples[PACKET_SAMPLES] = {s[0], s[0], s[1], s[1]};
  hdmi::Packet p;
  cs_frame = hdmi::samplePacket(p, samples, cs_frame);
  hdmi::encodeIsland(out, p, v);
}

void encodeIslands() {
  hdmi::Packet p;
  hdmi::nullPacket(p);
  hdmi::encodeIsland(null_island[0], p, hdmi::VSync::OFF);
  hdmi::encodeIsland(null_island[1], p, hdmi::VSync::PULSE);
  hdmi::acrPacket(p, ACR_N, ACR_CTS);
  hdmi::encodeIsland(acr_island[0], p, hdmi::VSync::OFF);
  hdmi::encodeIsland(acr_island[1], p, hdmi::VSync::PULSE);
  hdmi::aviInfoFrame(p);
  hdmi::encodeIsland(avi_island, p, hdmi::VSync::OFF);
  hdmi::audioInfoFrame(p);
  hdmi::encodeIsland(audio_info_island, p, hdmi::VSync::OFF);
}
#endif

} // namespace

void DviHstxVideo::flip(char flipY, char flipX) {
  const dvi::Orientation o = dvi::orientation(flipY, flipX);
  flip_x = o.x;
  flip_y = o.y;
}

void DviHstxVideo::submit(const uint16_t *strip, uint32_t pixels) {
  if (written >= STRIPS) {
    return;
  }

  // the screen follows the pass the beam is in (endFrame returns once it
  // read the first strip)
  if (written == 0) {
    render_base = dvi::passBase(beam, PASS_SPAN);
    render_start_us = time_us_32();
  }

  // wait until that pass has read the strip's rows; a stalled scanout
  // (endFrame resyncs it) holds the screen up only once
  const uint32_t read = render_base + (written + 1) * STRIP_ROWS;
  while ((int32_t)(beam - read) < 0 && time_us_32() - render_start_us < STALL_MS * 1000) {
  }

  // machines pack the strip at its width; framebuffer rows are SRC_WIDTH wide
  const uint32_t lines = SRC_ROWS / STRIPS;
  const uint32_t width = pixels / lines;
  for (uint32_t line = 0; line < lines; line++) {
    memcpy(frame[dvi::frameRow(written, line, flip_y)], strip + line * width, width * sizeof(uint16_t));
  }
  written++;
}

void DviHstxVideo::endFrame(uint8_t part, uint32_t) {
  // native: nativeStrip() paces the screens
  if (native_mode) {
#ifdef DEBUG_TIMING
    static uint32_t last_us;
    if (time_us_32() - last_us > 1000000) {
      printf("DVI: native misses %u late strips %u, frame max %u us, slot wait %u us/s\n",
             (unsigned)native_misses, (unsigned)native_late, (unsigned)native_frame_max_us,
             (unsigned)native_wait_us);
      native_frame_max_us = 0;
      native_wait_us = 0;
      last_us = time_us_32();
    }
#endif
    checkScanRate();
    return;
  }

  // Next screen: once the pass after the screen's last one read the first
  // strip. Half rate, second part: once the first part's pass read every
  // row; the second half then has to beat the next pass to row 144.
  const uint8_t parts = screenParts();
  const bool last = part + 1 >= parts;
  const uint32_t target = last ? render_base + parts * PASS_SPAN + STRIP_ROWS
                               : render_base + SRC_ROWS;
  const uint32_t start = time_us_32();
  while ((int32_t)(beam - target) < 0) {
    if (time_us_32() - start > STALL_MS * 1000) {
      resync();
      break;
    }
    xSemaphoreTake(wake, 1);
  }

  checkScanRate();
  if (last) {
    written = 0;
  }
}

// over-rate: the stream lost its framing (see wake_irq). The window grows
// past 1 s while a game loads.
void DviHstxVideo::checkScanRate() {
  const uint32_t now = time_us_32();
  if (now - rate_start_us >= 1000000) {
    if (dvi::overRate(scan_frames - rate_start_frames, now - rate_start_us, MAX_FPS)) {
      resync();
    }
#if defined(HAL_AUDIO_DVI) && defined(DEBUG_TIMING)
    static uint32_t reported;
    if (underruns != reported) {
      printf("HDMI: audio underruns %u\n", (unsigned)underruns);
      reported = underruns;
    }
#endif
    rate_start_us = time_us_32();
    rate_start_frames = scan_frames;
  }
}

// Builds active line `line` into line_buf[buffer] from the framebuffer;
// border lines are sent from black_line.
void HAL_FAST_CODE DviHstxVideo::buildLine(uint32_t line, uint32_t buffer) {
  if (native_mode) {
    buildNative(buffer);
    return;
  }
  if (line < PIC_TOP || line >= PIC_TOP + PIC_LINES) {
    return;
  }

  // a new width leaves pixels in what is now border: clear once
  uint32_t *out = line_buf[buffer] + HEADER_WORDS;
  const uint16_t width = viewport_width;
  if (built_width[buffer] != width) {
    clearWords(out, LINE_WORDS);
    built_width[buffer] = width;
  }

  uint32_t q = 0;
  const uint32_t row = DVI_SCALE == 1 ? line - PIC_TOP : dvi::scaleRow(line - PIC_TOP, SRC_ROWS, PIC_LINES, &q);
  const uint16_t *a = frame[row];
  const uint16_t *b = q ? frame[row + 1] : a;

  const uint32_t picWords = DVI_SCALE == 1 ? width / 2 : width;
  uint32_t *pic = out + (LINE_WORDS - picWords) / 2;
  if (DVI_SCALE == 1) {
    dvi::line1x(a, pic, width, flip_x);
  } else {
    dvi::line2x(a, b, q, pic, width, flip_x);
  }

  // rows this pass no longer reads (2x: the next line may read `row` again);
  // endFrame waits for the first strip and for the last row
  const uint32_t done = line == PIC_TOP + PIC_LINES - 1 ? SRC_ROWS : (DVI_SCALE == 1 ? row + 1 : row);
  const bool firstStrip = pass_rows < STRIP_ROWS && done >= STRIP_ROWS;
  pass_rows = done;
  beam = pass_base + done;
  if (firstStrip || done == SRC_ROWS) {
    signalWake();
  }
  if (done == SRC_ROWS) {
    pass_base += PASS_SPAN;
    pass_rows = 0;
  }
}

// Native: the next line from its strip at 1x, black if the strip is late
void HAL_FAST_CODE DviHstxVideo::buildNative(uint32_t buffer) {
  uint32_t *out = line_buf[buffer] + HEADER_WORDS;
  const uint32_t row = native_rows;
  const uint32_t strip = row / dvi::NATIVE_STRIP_ROWS;
  const uint32_t slot = dvi::ringSlot(strip);

  if (!dvi::stripReady(slot_tag[slot], strip)) {
    if (built_width[buffer] != 0) {
      clearWords(out, LINE_WORDS);
      built_width[buffer] = 0;
    }
    native_misses = native_misses + 1;
    native_rows = row + 1;
    return;
  }

  // a new width leaves pixels in what is now border: clear once
  const uint16_t width = native_width;
  if (built_width[buffer] != width) {
    clearWords(out, LINE_WORDS);
    built_width[buffer] = width;
  }
  const uint16_t *src = &frame[0][0] + (slot * dvi::NATIVE_STRIP_ROWS + row % dvi::NATIVE_STRIP_ROWS) * width;
  dvi::line1x(src, out + (LINE_WORDS - width / 2) / 2, width, dvi::Mirror::OFF);

  // counted after the copy: slotFree() hands the slot to the video core
  __dmb();
  native_rows = row + 1;
}

// a new width readdresses the ring: set while the resync holds the IRQ
void DviHstxVideo::setNative(uint16_t width) {
  if (native_mode && width == native_width) {
    return;
  }
  pending_width = width;
  switchMode(true);
}

void DviHstxVideo::switchMode(bool native) {
  pending_mode = native ? 1 : 0;
  resync();
}

uint16_t *DviHstxVideo::nativeStrip() {
  if (native_count == 0) {
    native_strip = dvi::firstStrip(native_rows);
#ifdef DEBUG_TIMING
    native_frame_start_us = time_us_32();
#endif
  }
  const uint32_t strip = native_strip + native_count;

  // a stalled scanout holds the screen up only once
  const uint32_t start = time_us_32();
  while (!dvi::slotFree(strip, native_rows)) {
    if (time_us_32() - start > STALL_MS * 1000) {
      resync();
      break;
    }
  }
#ifdef DEBUG_TIMING
  native_wait_us += time_us_32() - start;
#endif
  return &frame[0][0] + dvi::ringSlot(strip) * dvi::NATIVE_STRIP_ROWS * native_width;
}

void DviHstxVideo::nativeSubmit() {
  const uint32_t strip = native_strip + native_count;
#ifdef DEBUG_TIMING
  if ((int32_t)(native_rows - strip * dvi::NATIVE_STRIP_ROWS) > 0) {
    native_late++;
  }
  if (native_count + 1 == NATIVE_STRIPS) {
    const uint32_t us = time_us_32() - native_frame_start_us;
    native_frame_max_us = us > native_frame_max_us ? us : native_frame_max_us;
  }
#endif

  // pixels before the tag: the IRQ runs on the other core
  __dmb();
  slot_tag[dvi::ringSlot(strip)] = strip;
  native_count = native_count + 1 == NATIVE_STRIPS ? 0 : native_count + 1;
}

uint32_t DviHstxVideo::nativeMisses() const {
  return native_misses;
}

void HAL_FAST_CODE DviHstxVideo::irq() {
  irq_busy = true;
  __dmb();
  if (resyncing) {
    dma_hw->intr = (1u << ch_ping) | (1u << ch_pong);
  } else {
    scanout();
  }
  __dmb();
  irq_busy = false;
}

void HAL_FAST_CODE DviHstxVideo::scanout() {
  // pong: the channel that just finished, now queued two transfers ahead
  const int chNum = pong ? ch_pong : ch_ping;
  dma_channel_hw_t *ch = &dma_hw->ch[chNum];
  dma_hw->intr = 1u << chNum;
  pong = !pong;

#ifdef HAL_AUDIO_DVI
  audio_due += AUDIO_HZ;
#endif
  if (v_scanline < V_BLANK) {
    const bool vpulse = v_scanline >= V_FRONT_PORCH && v_scanline < V_FRONT_PORCH + V_SYNC_WIDTH;
#ifdef HAL_AUDIO_DVI
    // the requeued channel's own buffer: the other channel is sending its own
    uint32_t *buf = blank_buf[chNum == ch_pong];
    hdmi::lineHeader(buf, hdmi::Line::BLANK, vpulse ? hdmi::VSync::PULSE : hdmi::VSync::OFF);
    writeIsland(buf + hdmi::ISLAND_AT, held_blank[chNum == ch_pong], v_scanline);
    ch->read_addr = (uintptr_t)buf;
    ch->transfer_count = hdmi::BLANK_WORDS;
#else
    ch->read_addr = (uintptr_t)(vpulse ? vblank_vsync_on : vblank_vsync_off);
    ch->transfer_count = count_of(vblank_vsync_off);
#endif
  } else {
    // requeue first, then build: the line before this one is being sent
    const uint32_t line = v_scanline - V_BLANK;
    const bool inPicture = self->native_mode || (line >= PIC_TOP && line < PIC_TOP + PIC_LINES);
    uint32_t *buf = inPicture ? line_buf[line & 1] : black_line[line & 1];
    ch->read_addr = (uintptr_t)buf;
    ch->transfer_count = ACTIVE_WORDS + (line == V_ACTIVE - 1 ? TAIL_WORDS : 0);
#ifdef HAL_AUDIO_DVI
    writeIsland(buf + hdmi::ISLAND_AT, inPicture ? held_line[line & 1] : held_black[line & 1], v_scanline);
#endif
    self->buildLine(line, line & 1);
  }

  v_scanline = v_scanline + 1;
  if (v_scanline < V_TOTAL) {
    return;
  }
  // blank line 0 went out as the tail of the last active line
  v_scanline = 1;
#ifdef HAL_AUDIO_DVI
  audio_due += AUDIO_HZ;
#endif

  // vblank: every line of the frame is built, the next frame starts
  scan_frames = scan_frames + 1;
}

// Rebuilds the DMA chain from line 0 (after pico-galagino's hstx_resync).
void DviHstxVideo::resync() {
  const uint32_t both = (1u << ch_ping) | (1u << ch_pong);
  resyncing = true;
  __dmb();
  hw_clear_bits(&dma_hw->inte0, both);
  while (irq_busy) {
  }

  // RP2350-E5: clear EN on both channels before the abort, else the chained
  // partner can re-trigger the aborted channel with stale config
  hw_clear_bits(&dma_hw->ch[ch_ping].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
  hw_clear_bits(&dma_hw->ch[ch_pong].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
  dma_channel_abort(ch_ping);
  dma_channel_abort(ch_pong);

  // empty FIFO, then EN low resets the shifter and clock generator
  drainHstx();
  hw_clear_bits(&hstx_ctrl_hw->csr, HSTX_CTRL_CSR_EN_BITS);
  dma_hw->ints0 = both;
  hw_set_bits(&dma_hw->ch[ch_ping].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
  hw_set_bits(&dma_hw->ch[ch_pong].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
  queueBlankLines();

  // mode switch: the IRQ is off, the new mode starts with this pass
  if (pending_mode >= 0) {
    native_mode = pending_mode;
    if (native_mode) {
      native_width = pending_width;
    }
    pending_mode = -1;
    native_rows = 0;
    native_count = 0;
    for (uint32_t i = 0; i < dvi::RING_SLOTS; i++) {
      slot_tag[i] = NO_STRIP;
    }
    built_width[0] = built_width[1] = 0xffff;
    memset(frame, 0, sizeof(frame));
  }
  // native: the restarted pass begins at a whole pass
  native_rows = dvi::firstStrip(native_rows) * dvi::NATIVE_STRIP_ROWS;

  // a new pass starts at row 0
  pass_base = (beam & ~(PASS_SPAN - 1)) + PASS_SPAN;
  pass_rows = 0;
  beam = pass_base;

  scan_frames = scan_frames + 1;

  hw_set_bits(&hstx_ctrl_hw->csr, HSTX_CTRL_CSR_EN_BITS);
  hw_set_bits(&dma_hw->inte0, both);
  __dmb();
  resyncing = false;
  dma_channel_start(ch_ping);
  resyncs++;
  printf("DVI: scanout resync %u\n", (unsigned)resyncs);
}

void DviHstxVideo::enableIrqs(void *) {
  wake_irq = user_irq_claim_unused(true);
  irq_set_exclusive_handler(wake_irq, wakeIrq);
  irq_set_enabled(wake_irq, true);
  irq_set_exclusive_handler(DMA_IRQ_0, &DviHstxVideo::irq);
  irq_set_priority(DMA_IRQ_0, PICO_HIGHEST_IRQ_PRIORITY);
  irq_set_enabled(DMA_IRQ_0, true);
  xSemaphoreGive(irqs_ready);
  taskExit();
}

void DviHstxVideo::begin() {
  self = this;
  wake = xSemaphoreCreateBinary();
  for (uint32_t b = 0; b < 2; b++) {
#ifdef HAL_AUDIO_DVI
    hdmi::lineHeader(line_buf[b], hdmi::Line::ACTIVE, hdmi::VSync::OFF);
    hdmi::lineHeader(black_line[b], hdmi::Line::ACTIVE, hdmi::VSync::OFF);
#else
    for (uint32_t i = 0; i < HEADER_WORDS; i++) {
      line_buf[b][i] = black_line[b][i] = vactive_line[i];
    }
#endif
    for (uint32_t i = 0; i < TAIL_WORDS; i++) {
      line_buf[b][ACTIVE_WORDS + i] = black_line[b][ACTIVE_WORDS + i] = vblank_vsync_off[i];
    }
  }
#ifdef HAL_AUDIO_DVI
  encodeIslands();
#endif
  // two bits per HSTX clock: 252 Mbit/s per lane from a 252 MHz clk_sys
  clock_configure_int_divider(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                              clock_get_hz(clk_sys), 2);
  if (clock_get_hz(clk_hstx) != HSTX_HZ) {
    printf("DVI: clk_hstx %u Hz, expected %u (clk_sys must be 252 MHz)\n",
           (unsigned)clock_get_hz(clk_hstx), (unsigned)HSTX_HZ);
  }

  // TMDS encoder for RGB565: lane 2 red (15:11), 1 green (10:5), 0 blue
  // (4:0), each rotated so its MSB lands on bit 7; two pixels per word.
  hstx_ctrl_hw->expand_tmds =
      4 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB | 8 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
      5 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB | 3 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
      4 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB | 29 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;
  hstx_ctrl_hw->expand_shift =
      2 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB | 16 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
      1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB | 0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

  // clock period 5 HSTX cycles, pop the expander every 5, shift 2 bits per cycle
  hstx_ctrl_hw->csr = 0;
  hstx_ctrl_hw->csr = HSTX_CTRL_CSR_EXPAND_EN_BITS | 5u << HSTX_CTRL_CSR_CLKDIV_LSB |
                      5u << HSTX_CTRL_CSR_N_SHIFTS_LSB | 2u << HSTX_CTRL_CSR_SHIFT_LSB |
                      HSTX_CTRL_CSR_EN_BITS;

  // pairs from the board profile: P on the given pin, N on the next one
  const uint32_t ck = HSTX_CK_PIN - HSTX_FIRST_PIN;
  hstx_ctrl_hw->bit[ck] = HSTX_CTRL_BIT0_CLK_BITS;
  hstx_ctrl_hw->bit[ck + 1] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
  const uint32_t lanePin[3] = {HSTX_D0_PIN, HSTX_D1_PIN, HSTX_D2_PIN};
  for (uint32_t lane = 0; lane < 3; lane++) {
    // even bits in the first half of each HSTX cycle, odd bits in the second
    const uint32_t bit = lanePin[lane] - HSTX_FIRST_PIN;
    const uint32_t sel = (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB |
                         (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
    hstx_ctrl_hw->bit[bit] = sel;
    hstx_ctrl_hw->bit[bit + 1] = sel | HSTX_CTRL_BIT0_INV_BITS;
  }
  for (uint32_t pin = HSTX_FIRST_PIN; pin < HSTX_FIRST_PIN + 8; pin++) {
    gpio_set_function(pin, GPIO_FUNC_HSTX);
  }

  // two channels, each chained to the other; the IRQ requeues the one that
  // finished while the other runs
  ch_ping = dma_claim_unused_channel(true);
  ch_pong = dma_claim_unused_channel(true);
  dma_channel_config c = dma_channel_get_default_config(ch_ping);
  channel_config_set_chain_to(&c, ch_pong);
  channel_config_set_dreq(&c, DREQ_HSTX);
  dma_channel_configure(ch_ping, &c, &hstx_fifo_hw->fifo, vblank_vsync_off,
                        count_of(vblank_vsync_off), false);
  c = dma_channel_get_default_config(ch_pong);
  channel_config_set_chain_to(&c, ch_ping);
  channel_config_set_dreq(&c, DREQ_HSTX);
  dma_channel_configure(ch_pong, &c, &hstx_fifo_hw->fifo, vblank_vsync_off,
                        count_of(vblank_vsync_off), false);

  // NVIC enables are per core: a short task enables both IRQs over there
  dma_hw->ints0 = (1u << ch_ping) | (1u << ch_pong);
  dma_hw->inte0 = (1u << ch_ping) | (1u << ch_pong);
  irqs_ready = xSemaphoreCreateBinary();
  taskSpawn(enableIrqs, nullptr, "dvi irq", 1024, configMAX_PRIORITIES - 1, EMULATION_CORE);
  xSemaphoreTake(irqs_ready, portMAX_DELAY);
  bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

  rate_start_us = time_us_32();
  dma_channel_start(ch_ping);
#ifdef HAL_AUDIO_DVI
  const char *mode = "HDMI";
#else
  const char *mode = "DVI";
#endif
  printf("%s 640x480 60 Hz, DVI_SCALE %d, picture %u lines from %u\n", mode, DVI_SCALE,
         (unsigned)PIC_LINES, (unsigned)PIC_TOP);
}

} // namespace hal

#endif // HAL_VIDEO_DVI_HSTX
#endif // HAL_RP2350
