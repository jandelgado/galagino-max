// HSTX DVI scanout. Timing, command lists and the ping-pong DMA follow
// pico-examples hstx/dvi_out_hstx_encoder (BSD-3-Clause, Raspberry Pi); the
// pin mapping loop follows pico-galagino's pico_hdmi/video_output.c (GPLv3).
#ifdef HAL_RP2350
#include "../../config.h"  // the board profile selects the backend
#ifdef HAL_VIDEO_DVI_HSTX
#include "video_dvi_hstx.h"
#include <stdio.h>
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
#include <pico/time.h>
#include "../hal_attr.h"

namespace hal {

namespace {

// 640x480 60 Hz (CEA-861 VIC 1): 25.2 MHz pixel clock, 10 bits per pixel
// and lane, two bits per HSTX clock: clk_hstx = 126 MHz.
const uint32_t HSTX_HZ = 126000000;
const uint32_t H_FRONT_PORCH = 16;
const uint32_t H_SYNC_WIDTH = 96;
const uint32_t H_BACK_PORCH = 48;
const uint32_t H_ACTIVE = 640;
const uint32_t V_FRONT_PORCH = 10;
const uint32_t V_SYNC_WIDTH = 2;
const uint32_t V_BACK_PORCH = 33;
const uint32_t V_ACTIVE = dvi::OUT_LINES;
const uint32_t V_BLANK = V_FRONT_PORCH + V_SYNC_WIDTH + V_BACK_PORCH;
const uint32_t V_TOTAL = V_BLANK + V_ACTIVE;
const uint32_t HSTX_FIRST_PIN = 12;

// TMDS control symbols: sync levels on lane 0, the other lanes idle
const uint32_t TMDS_CTRL_00 = 0x354u;
const uint32_t TMDS_CTRL_01 = 0x0abu;
const uint32_t TMDS_CTRL_10 = 0x154u;
const uint32_t TMDS_CTRL_11 = 0x2abu;
const uint32_t SYNC_V0_H0 = TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20);
const uint32_t SYNC_V0_H1 = TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20);
const uint32_t SYNC_V1_H0 = TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20);
const uint32_t SYNC_V1_H1 = TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20);

const uint32_t CMD_RAW_REPEAT = 0x1u << 12;
const uint32_t CMD_TMDS = 0x2u << 12;
const uint32_t CMD_NOP = 0xfu << 12;

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
uint32_t vactive_line[] = {
  CMD_RAW_REPEAT | H_FRONT_PORCH, SYNC_V1_H1, CMD_NOP,
  CMD_RAW_REPEAT | H_SYNC_WIDTH, SYNC_V1_H0, CMD_NOP,
  CMD_RAW_REPEAT | H_BACK_PORCH, SYNC_V1_H1,
  CMD_TMDS | H_ACTIVE
};

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
const uint32_t HEADER_WORDS = count_of(vactive_line);
const uint32_t ACTIVE_WORDS = HEADER_WORDS + LINE_WORDS;
const uint32_t TAIL_WORDS = count_of(vblank_vsync_off);

// memset lives in flash: the IRQ clears with this loop from RAM
__attribute__((optimize("no-tree-loop-distribute-patterns")))
HAL_FAST_CODE void clearWords(uint32_t *words, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    words[i] = 0;
  }
}

// The DMA IRQ has the highest priority: a late requeue makes a channel
// replay a stale buffer, the HSTX command stream loses its framing and stays
// garbled (about 160 frames/s, no picture). At that priority it may not call
// FreeRTOS, so it pends wake_irq, a low priority IRQ that gives the semaphore.
DviHstxVideo *self;
SemaphoreHandle_t wake;          // given at vblank and when a strip frees
int wake_irq;
volatile uint32_t scan_frames;   // vblanks so far

// Scanout watchdog (endFrame): no vblank for STALL_MS, or more than
// MAX_FPS frames per second, means the DMA chain lost its framing.
const uint32_t STALL_MS = 100;
const uint32_t MAX_FPS = 75;
uint32_t rate_start_us;
uint32_t rate_start_frames;
uint32_t resyncs;

// Ping-pong line buffers: the IRQ builds line n + 1 while line n is sent.
uint32_t line_buf[2][ACTIVE_WORDS + TAIL_WORDS];
uint16_t built_width[2];
uint32_t black_line[ACTIVE_WORDS + TAIL_WORDS];

int ch_ping;
int ch_pong;
bool pong;
uint32_t v_scanline = 2;  // ping and pong are queued before the first IRQ
uint16_t released_strip;

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

} // namespace

void DviHstxVideo::flip(char flipY, char flipX) {
  flip_y = flipY ? dvi::Mirror::ON : dvi::Mirror::OFF;
  flip_x = flipX ? dvi::Mirror::ON : dvi::Mirror::OFF;
}

void DviHstxVideo::submit(const uint16_t *strip, uint32_t pixels) {
  uint16_t *slot;
  while (!(slot = ring.tryAcquire())) {
    xSemaphoreTake(wake, 1);
  }

  // machines pack the strip at its width; the ring rows are 240 wide
  const uint32_t width = pixels / dvi::StripRing<RING_SLOTS>::STRIP_LINES;
  for (uint32_t line = 0; line < dvi::StripRing<RING_SLOTS>::STRIP_LINES; line++) {
    dvi::copySwapped(strip + line * width, slot + line * dvi::StripRing<RING_SLOTS>::WIDTH, width);
  }
  ring.publish();
}

void DviHstxVideo::endFrame(uint8_t, uint32_t) {
  const uint32_t frame = scan_frames;
  const uint32_t start = time_us_32();
  while (scan_frames == frame) {
    if (time_us_32() - start > STALL_MS * 1000) {
      resync();
      break;
    }
    xSemaphoreTake(wake, 1);
  }

  // over-rate: the stream lost its framing (see wake_irq)
  const uint32_t now = time_us_32();
  if (now - rate_start_us >= 1000000) {
    if (scan_frames - rate_start_frames > MAX_FPS) {
      resync();
    }
    rate_start_us = time_us_32();
    rate_start_frames = scan_frames;
  }
  ring.startWrite();

#ifdef DEBUG_TIMING
  // strips that came after the beam needed them (black lines on screen)
  if (scan_frames - frames_reported >= 600) {
    printf("DVI late lines %u in %u frames\n", (unsigned)ring.lateLines(),
           (unsigned)(scan_frames - frames_reported));
    ring.clearLate();
    frames_reported = scan_frames;
  }
#endif
}

// Builds active line `line` into line_buf[buffer] from the ring; border
// lines are sent from black_line.
void HAL_FAST_CODE DviHstxVideo::buildLine(uint32_t line, uint32_t buffer) {
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
  const uint16_t *a = ring.row(row, flip_y);
  const uint16_t *b = q ? ring.row(row + 1, flip_y) : a;
  ring.release(row);

  // the writer moved past a strip: submit() may continue
  const uint16_t strip = row / dvi::StripRing<RING_SLOTS>::STRIP_LINES;
  if (strip != released_strip) {
    released_strip = strip;
    signalWake();
  }

  const uint32_t picWords = DVI_SCALE == 1 ? width / 2 : width;
  uint32_t *pic = out + (LINE_WORDS - picWords) / 2;
  if (!a || !b) {
    clearWords(pic, picWords);
    return;
  }
  if (DVI_SCALE == 1) {
    dvi::line1x(a, pic, width, flip_x);
  } else {
    dvi::line2x(a, b, q, pic, width, flip_x);
  }
}

void HAL_FAST_CODE DviHstxVideo::irq() {
  // pong: the channel that just finished, now queued two transfers ahead
  const int chNum = pong ? ch_pong : ch_ping;
  dma_channel_hw_t *ch = &dma_hw->ch[chNum];
  dma_hw->intr = 1u << chNum;
  pong = !pong;

  if (v_scanline >= V_FRONT_PORCH && v_scanline < V_FRONT_PORCH + V_SYNC_WIDTH) {
    ch->read_addr = (uintptr_t)vblank_vsync_on;
    ch->transfer_count = count_of(vblank_vsync_on);
  } else if (v_scanline < V_BLANK) {
    ch->read_addr = (uintptr_t)vblank_vsync_off;
    ch->transfer_count = count_of(vblank_vsync_off);
  } else {
    // requeue first, then build: the line before this one is being sent
    const uint32_t line = v_scanline - V_BLANK;
    const bool inPicture = line >= PIC_TOP && line < PIC_TOP + PIC_LINES;
    ch->read_addr = (uintptr_t)(inPicture ? line_buf[line & 1] : black_line);
    ch->transfer_count = ACTIVE_WORDS + (line == V_ACTIVE - 1 ? TAIL_WORDS : 0);
    self->buildLine(line, line & 1);
  }

  v_scanline = v_scanline + 1;
  if (v_scanline < V_TOTAL) {
    return;
  }
  // blank line 0 went out as the tail of the last active line
  v_scanline = 1;

  // vblank: every line of the frame is built, the next frame starts
  self->ring.startScan();
  released_strip = 0;
  scan_frames = scan_frames + 1;
  signalWake();
}

// Rebuilds the DMA chain from line 0 (after pico-galagino's hstx_resync).
void DviHstxVideo::resync() {
  irq_set_enabled(DMA_IRQ_0, false);

  // RP2350-E5: clear EN on both channels before the abort, else the chained
  // partner can re-trigger the aborted channel with stale config
  const uint32_t both = (1u << ch_ping) | (1u << ch_pong);
  hw_clear_bits(&dma_hw->ch[ch_ping].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
  hw_clear_bits(&dma_hw->ch[ch_pong].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
  dma_channel_abort(ch_ping);
  dma_channel_abort(ch_pong);

  // EN low flushes the FIFO and resets the shifter and clock generator
  hw_clear_bits(&hstx_ctrl_hw->csr, HSTX_CTRL_CSR_EN_BITS);
  dma_hw->ints0 = both;
  hw_set_bits(&dma_hw->ch[ch_ping].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
  hw_set_bits(&dma_hw->ch[ch_pong].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
  queueBlankLines();

  // the frame being written is lost: black until the next one
  ring.startScan();
  released_strip = 0;
  scan_frames = scan_frames + 1;

  hw_set_bits(&hstx_ctrl_hw->csr, HSTX_CTRL_CSR_EN_BITS);
  irq_set_enabled(DMA_IRQ_0, true);
  dma_channel_start(ch_ping);
  resyncs++;
  printf("DVI: scanout resync %u\n", (unsigned)resyncs);
}

void DviHstxVideo::begin() {
  self = this;
  wake = xSemaphoreCreateBinary();
  for (uint32_t i = 0; i < HEADER_WORDS; i++) {
    line_buf[0][i] = line_buf[1][i] = black_line[i] = vactive_line[i];
  }
  for (uint32_t i = 0; i < TAIL_WORDS; i++) {
    line_buf[0][ACTIVE_WORDS + i] = line_buf[1][ACTIVE_WORDS + i] =
        black_line[ACTIVE_WORDS + i] = vblank_vsync_off[i];
  }
  wake_irq = user_irq_claim_unused(true);
  irq_set_exclusive_handler(wake_irq, wakeIrq);
  irq_set_enabled(wake_irq, true);

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

  // the IRQ runs on this core, the video core
  dma_hw->ints0 = (1u << ch_ping) | (1u << ch_pong);
  dma_hw->inte0 = (1u << ch_ping) | (1u << ch_pong);
  irq_set_exclusive_handler(DMA_IRQ_0, &DviHstxVideo::irq);
  irq_set_priority(DMA_IRQ_0, PICO_HIGHEST_IRQ_PRIORITY);
  irq_set_enabled(DMA_IRQ_0, true);
  bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

  rate_start_us = time_us_32();
  dma_channel_start(ch_ping);
  printf("DVI 640x480 60 Hz, DVI_SCALE %d, picture %u lines from %u\n", DVI_SCALE,
         (unsigned)PIC_LINES, (unsigned)PIC_TOP);
}

} // namespace hal

#endif // HAL_VIDEO_DVI_HSTX
#endif // HAL_RP2350
