#ifndef WILLIAMS_H
#define WILLIAMS_H

#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"
#include "williams_board.h"
#include "williams_video.h"

static_assert(BOARD_KEY_LEFT == BUTTON_LEFT && BOARD_KEY_RIGHT == BUTTON_RIGHT &&
              BOARD_KEY_UP == BUTTON_UP && BOARD_KEY_DOWN == BUTTON_DOWN &&
              BOARD_KEY_FIRE == BUTTON_FIRE && BOARD_KEY_START == BUTTON_START &&
              BOARD_KEY_COIN == BUTTON_COIN && BOARD_KEY_EXTRA == BUTTON_EXTRA &&
              BOARD_KEY_A == BUTTON_A && BOARD_KEY_B == BUTTON_B &&
              BOARD_KEY_X == BUTTON_X && BOARD_KEY_Y == BUTTON_Y, "key bits");

// Galagino side of the Williams gen 1 machines: forwards the 6809 bus to
// the board, renders the scanout through the column map, feeds the DAC.
// Native 292 x 240 (ROT0) -> 239 x 240, rows 24..263 of the 288.
class williams : public machineBase {
public:
  void start() override;
  void reset() override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  const int renderWidth() override { return ColumnMap::OUT_MAX; }
  const int renderBuffer() override { return ColumnMap::OUT_MAX * 2 * 8; }
  int renderDrumSample() override;

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

protected:
  // board is a member of the derived class, not yet built: store only
  williams(WilliamsBoard &board, const ColumnLayout &columns);
  void setPalette(const uint16_t *pal) { rgb = pal; }
  WilliamsBoard &board;

private:
  static const int TOP_ROW = 24;    // (288 - 240) / 2
  static const int DAC_VOLUME = 4;  // 8 bit DAC -> +-512 mix range
  static const int PENS = 16;
  static const uint16_t CMOS_OFS = 0x0000, SOUND_RAM_OFS = 0x0400;  // memory[]

  void clearPads(uint16_t *line);

  ColumnMap columns;
  const uint16_t *rgb = nullptr;
  uint8_t shown = 0;
  uint16_t pens[Scanout::BANDS][PENS];
  int lastSample = 0;
};

#endif
