#ifndef HAL_RP2350_HDMI_LINE_H
#define HAL_RP2350_HDMI_LINE_H

// HSTX command lists of one 640x480 60 Hz line (CEA-861 VIC 1), DVI and
// HDMI. An HDMI line carries a data island (hdmi_packet.h) in the hsync
// pulse, and its active part starts with the video preamble and guard band.
// No SDK dependencies, so host tests cover them. HDMI layout after
// pico-galagino's pico_shared/drivers/pico_hdmi/video_output.c (GPLv3).
//
//   clock 0    16        24          60          112            150  158 160      800
//         | FP | preamble | island 36 | hsync rest | back porch   | VP | G | pixels |
//              |<------------- hsync pulse ------>|

#include <stdint.h>
#include "hdmi_packet.h"

namespace hal {
namespace hstx {

const uint32_t H_FRONT_PORCH = 16;
const uint32_t H_SYNC_WIDTH = 96;
const uint32_t H_BACK_PORCH = 48;
const uint32_t H_ACTIVE = 640;

const uint32_t CMD_RAW = 0x0u << 12;
const uint32_t CMD_RAW_REPEAT = 0x1u << 12;
const uint32_t CMD_TMDS = 0x2u << 12;
const uint32_t CMD_NOP = 0xfu << 12;

// TMDS control symbols, CTRL_<vsync><hsync> levels (both active low)
const uint32_t TMDS_CTRL_00 = 0x354u;
const uint32_t TMDS_CTRL_01 = 0x0abu;
const uint32_t TMDS_CTRL_10 = 0x154u;
const uint32_t TMDS_CTRL_11 = 0x2abu;

// sync levels on lane 0, the other lanes idle
const uint32_t SYNC_V0_H0 = TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20);
const uint32_t SYNC_V0_H1 = TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20);
const uint32_t SYNC_V1_H0 = TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20);
const uint32_t SYNC_V1_H1 = TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20);

} // namespace hstx

namespace hdmi {

const uint32_t W_PREAMBLE = 8;
const uint32_t W_VIDEO_GUARD = 2;

// preambles: CTL0..3 on lanes 1 and 2 announce a data island (1, 0, 1, 0)
// or video (1, 0, 0, 0)
const uint32_t DATA_PREAMBLE_V0 = hstx::TMDS_CTRL_00 | (hstx::TMDS_CTRL_01 << 10) | (hstx::TMDS_CTRL_01 << 20);
const uint32_t DATA_PREAMBLE_V1 = hstx::TMDS_CTRL_10 | (hstx::TMDS_CTRL_01 << 10) | (hstx::TMDS_CTRL_01 << 20);
const uint32_t VIDEO_PREAMBLE = hstx::TMDS_CTRL_11 | (hstx::TMDS_CTRL_01 << 10) | (hstx::TMDS_CTRL_00 << 20);
const uint32_t VIDEO_GUARD = 0x2ccu | (0x133u << 10) | (0x2ccu << 20);

enum class Line : uint8_t { BLANK, ACTIVE };

// word offset of the island in a line's command list
const uint32_t ISLAND_AT = 7;
const uint32_t BLANK_WORDS = ISLAND_AT + ISLAND_WORDS + 7;
// up to and with the TMDS command, the pixels follow
const uint32_t ACTIVE_WORDS = ISLAND_AT + ISLAND_WORDS + 13;

// Command list of a line, the island slot left as it is. Active lines are
// never in the vsync pulse. Returns the words written.
static inline HAL_FAST_CODE uint32_t lineHeader(uint32_t *out, Line kind, VSync v) {
  using namespace hstx;
  const bool vpulse = v == VSync::PULSE;
  uint32_t *p = out;
  *p++ = CMD_RAW_REPEAT | H_FRONT_PORCH;
  *p++ = vpulse ? SYNC_V0_H1 : SYNC_V1_H1;
  *p++ = CMD_NOP;
  *p++ = CMD_RAW_REPEAT | W_PREAMBLE;
  *p++ = vpulse ? DATA_PREAMBLE_V0 : DATA_PREAMBLE_V1;
  *p++ = CMD_NOP;
  *p++ = CMD_RAW | ISLAND_WORDS;
  p += ISLAND_WORDS;
  *p++ = CMD_NOP;
  *p++ = CMD_RAW_REPEAT | (H_SYNC_WIDTH - W_PREAMBLE - ISLAND_WORDS);
  *p++ = vpulse ? SYNC_V0_H0 : SYNC_V1_H0;
  *p++ = CMD_NOP;

  if (kind == Line::BLANK) {
    *p++ = CMD_RAW_REPEAT | (H_BACK_PORCH + H_ACTIVE);
    *p++ = vpulse ? SYNC_V0_H1 : SYNC_V1_H1;
    *p++ = CMD_NOP;
    return (uint32_t)(p - out);
  }

  *p++ = CMD_RAW_REPEAT | (H_BACK_PORCH - W_PREAMBLE - W_VIDEO_GUARD);
  *p++ = SYNC_V1_H1;
  *p++ = CMD_NOP;
  *p++ = CMD_RAW_REPEAT | W_PREAMBLE;
  *p++ = VIDEO_PREAMBLE;
  *p++ = CMD_NOP;
  *p++ = CMD_RAW_REPEAT | W_VIDEO_GUARD;
  *p++ = VIDEO_GUARD;
  *p++ = CMD_TMDS | H_ACTIVE;
  return (uint32_t)(p - out);
}

} // namespace hdmi
} // namespace hal

#endif // HAL_RP2350_HDMI_LINE_H
