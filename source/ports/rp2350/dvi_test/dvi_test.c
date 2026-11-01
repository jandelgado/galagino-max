// DVI test image for RP2350 boards with an HSTX DVI connector: colour bars on
// top, a grey ramp below, white frame, 640x480 60 Hz. Bare metal (no FreeRTOS,
// no galagino), same HSTX setup and pins as the galagino DVI backend
// (src/hal/rp2350/video_dvi_hstx.cpp), so it separates wiring and TV problems
// from the emulator. Prints the measured frame rate on USB serial once per
// second.
//
// Based on pico-examples hstx/dvi_out_hstx_encoder (BSD-3-Clause).
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include BOARD_PROFILE

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define H_FRONT_PORCH 16
#define H_SYNC_WIDTH  96
#define H_BACK_PORCH  48
#define H_ACTIVE      640
#define V_FRONT_PORCH 10
#define V_SYNC_WIDTH  2
#define V_BACK_PORCH  33
#define V_ACTIVE      480
#define V_BLANK       (V_FRONT_PORCH + V_SYNC_WIDTH + V_BACK_PORCH)
#define V_TOTAL       (V_BLANK + V_ACTIVE)

#define HSTX_CMD_RAW_REPEAT (0x1u << 12)
#define HSTX_CMD_TMDS       (0x2u << 12)
#define HSTX_CMD_NOP        (0xfu << 12)

#define LINE_WORDS (H_ACTIVE / 2)  // RGB565, two pixels per word
#define HEADER_WORDS 9             // vactive_line
#define TAIL_WORDS 7               // vblank_line_vsync_off
#define ACTIVE_WORDS (HEADER_WORDS + LINE_WORDS)
#define HSTX_FIRST_PIN 12

static uint32_t vblank_line_vsync_off[] = {
    HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH, SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH, SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT | (H_BACK_PORCH + H_ACTIVE), SYNC_V1_H1,
    HSTX_CMD_NOP
};
static uint32_t vblank_line_vsync_on[] = {
    HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH, SYNC_V0_H1,
    HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH, SYNC_V0_H0,
    HSTX_CMD_RAW_REPEAT | (H_BACK_PORCH + H_ACTIVE), SYNC_V0_H1,
    HSTX_CMD_NOP
};
static uint32_t vactive_line[] = {
    HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH, SYNC_V1_H1, HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH, SYNC_V1_H0, HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | H_BACK_PORCH, SYNC_V1_H1,
    HSTX_CMD_TMDS | H_ACTIVE
};

// The picture: four kinds of line, chosen per scanline. Each active line is
// one DMA transfer (commands, then pixels), the last one also carries blank
// line 0: a short transfer after pixels leaves the IRQ under 1 us to requeue,
// and a late requeue garbles the stream for good (see the galagino backend).
static uint32_t line_border[ACTIVE_WORDS + TAIL_WORDS];
static uint32_t line_bars[ACTIVE_WORDS + TAIL_WORDS];
static uint32_t line_ramp[ACTIVE_WORDS + TAIL_WORDS];
static uint32_t line_black[ACTIVE_WORDS + TAIL_WORDS];

static int ch_ping, ch_pong;
static bool dma_pong = false;
static uint v_scanline = 2;  // ping and pong are queued before the first IRQ
static volatile uint32_t frames;

static const uint32_t *line_for(uint active) {
    if (active < 4 || active >= V_ACTIVE - 4) {
        return line_border;
    }
    if (active < V_ACTIVE / 2) {
        return line_bars;
    }
    return active < V_ACTIVE - 40 ? line_ramp : line_black;
}

static void __not_in_flash_func(dma_irq_handler)(void) {
    // dma_pong: the channel that just finished, requeued two transfers ahead
    int ch_num = dma_pong ? ch_pong : ch_ping;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    dma_pong = !dma_pong;

    if (v_scanline >= V_FRONT_PORCH && v_scanline < V_FRONT_PORCH + V_SYNC_WIDTH) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < V_BLANK) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else {
        const uint active = v_scanline - V_BLANK;
        ch->read_addr = (uintptr_t)line_for(active);
        ch->transfer_count = ACTIVE_WORDS + (active == V_ACTIVE - 1 ? TAIL_WORDS : 0);
    }

    v_scanline++;
    if (v_scanline == V_TOTAL) {
        v_scanline = 1;  // blank line 0 went out with the last active line
        frames++;
    }
}

static uint16_t rgb565(uint r, uint g, uint b) {
    return (uint16_t)((r & 0xf8) << 8 | (g & 0xfc) << 3 | (b & 0xf8) >> 3);
}

static void set_pixel(uint32_t *line, uint x, uint16_t c) {
    // first pixel of a word in the low half
    line += HEADER_WORDS;
    if (x & 1) {
        line[x / 2] = (line[x / 2] & 0xffffu) | (uint32_t)c << 16;
    } else {
        line[x / 2] = (line[x / 2] & 0xffff0000u) | c;
    }
}

static void draw_lines(void) {
    // white, yellow, cyan, green, magenta, red, blue, black
    static const uint8_t bars[8][3] = {
        {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
        {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0}
    };
    uint32_t *lines[4] = {line_border, line_bars, line_ramp, line_black};
    for (uint n = 0; n < 4; n++) {
        for (uint i = 0; i < HEADER_WORDS; i++) {
            lines[n][i] = vactive_line[i];
        }
        for (uint i = 0; i < TAIL_WORDS; i++) {
            lines[n][ACTIVE_WORDS + i] = vblank_line_vsync_off[i];
        }
    }
    const uint16_t white = rgb565(255, 255, 255);
    for (uint x = 0; x < H_ACTIVE; x++) {
        const bool edge = x < 4 || x >= H_ACTIVE - 4;
        const uint8_t *bar = bars[x / (H_ACTIVE / 8)];
        const uint grey = x * 256 / H_ACTIVE;
        set_pixel(line_border, x, white);
        set_pixel(line_bars, x, edge ? white : rgb565(bar[0], bar[1], bar[2]));
        set_pixel(line_ramp, x, edge ? white : rgb565(grey, grey, grey));
        set_pixel(line_black, x, edge ? white : 0);
    }
}

static void hstx_setup(void) {
    // 25.2 MHz pixel clock: 10 bits per pixel, two bits per HSTX clock
#ifndef DEFAULT_CLOCKS
    clock_configure_int_divider(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                                clock_get_hz(clk_sys), 2);
#endif

    // RGB565: lane 2 red (15:11), 1 green (10:5), 0 blue (4:0)
    hstx_ctrl_hw->expand_tmds =
        4 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB | 8 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
        5 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB | 3 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
        4 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB | 29 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;
    hstx_ctrl_hw->expand_shift =
        2 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB | 16 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB | 0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr = HSTX_CTRL_CSR_EXPAND_EN_BITS | 5u << HSTX_CTRL_CSR_CLKDIV_LSB |
                        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB | 2u << HSTX_CTRL_CSR_SHIFT_LSB |
                        HSTX_CTRL_CSR_EN_BITS;

    // pairs from the board profile: P on the given pin, N on the next one
    const int ck = HSTX_CK_PIN - HSTX_FIRST_PIN;
    hstx_ctrl_hw->bit[ck] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[ck + 1] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    const int lane_pin[3] = {HSTX_D0_PIN, HSTX_D1_PIN, HSTX_D2_PIN};
    for (uint lane = 0; lane < 3; ++lane) {
        const int bit = lane_pin[lane] - HSTX_FIRST_PIN;
        const uint32_t sel = (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB |
                             (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[bit] = sel;
        hstx_ctrl_hw->bit[bit + 1] = sel | HSTX_CTRL_BIT0_INV_BITS;
    }
    for (int pin = HSTX_FIRST_PIN; pin < HSTX_FIRST_PIN + 8; ++pin) {
        gpio_set_function(pin, GPIO_FUNC_HSTX);
    }
}

static void dma_setup(void) {
    ch_ping = dma_claim_unused_channel(true);
    ch_pong = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(ch_ping);
    channel_config_set_chain_to(&c, ch_pong);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(ch_ping, &c, &hstx_fifo_hw->fifo, vblank_line_vsync_off,
                          count_of(vblank_line_vsync_off), false);
    c = dma_channel_get_default_config(ch_pong);
    channel_config_set_chain_to(&c, ch_ping);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(ch_pong, &c, &hstx_fifo_hw->fifo, vblank_line_vsync_off,
                          count_of(vblank_line_vsync_off), false);

    dma_hw->ints0 = (1u << ch_ping) | (1u << ch_pong);
    dma_hw->inte0 = (1u << ch_ping) | (1u << ch_pong);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_priority(DMA_IRQ_0, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
}

int main(void) {
#ifndef DEFAULT_CLOCKS
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(2);
    set_sys_clock_khz(252000, true);
#endif
    stdio_init_all();

    draw_lines();
    hstx_setup();
    dma_setup();
    dma_channel_start(ch_ping);

    uint32_t last = frames;
    for (;;) {
        sleep_ms(1000);
        const uint32_t now = frames;
        printf("dvi_test: %u frames/s, clk_sys %u kHz, clk_hstx %u kHz, fifo stat %08x\n",
               (unsigned)(now - last), (unsigned)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS),
               (unsigned)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_HSTX),
               (unsigned)hstx_fifo_hw->stat);
        last = now;
    }
}
