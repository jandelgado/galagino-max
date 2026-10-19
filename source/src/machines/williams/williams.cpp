#include "williams.h"
#include "williams_palette.h"

static_assert(0x0400 + 0x100 <= RAMSIZE, "CMOS + sound RAM in memory[]");

// 512 B palette: unpacks first in the Arena, before the game's assets
williams::williams(WilliamsBoard &b, const ColumnLayout &l)
  : board(b), williams_rgb(williams_rgb_blob), rgb(williams_rgb.data()) {
  columns.build(l);
}

void williams::reset() {
  machineBase::reset();  // clears memory[] (CMOS, sound RAM live there)
  board.bindMemory(memory + CMOS_OFS, memory + SOUND_RAM_OFS);
  board.reset();
  shown = board.scanout().front();
  lastSample = 0;
}

void williams::start() {
  reset();
}

void williams::run_frame() {
  board.setKeys(input->buttons_get());
  board.runFrame();
  if (board.booted()) {
    game_started = 1;  // boot (RAM test, factory restore) ran at full speed
  }
}

// latch the finished scanout and its per-band pens once per frame
void williams::prepare_frame() {
  const Scanout &s = board.scanout();
  shown = s.front();
  for (int band = 0; band < Scanout::BANDS; band++) {
    const uint8_t *p = s.palette(shown, band * Scanout::BAND_LINES);
    for (int i = 0; i < PENS; i++) {
      pens[band][i] = rgb[p[i]];
    }
  }
}

void IRAM_ATTR williams::render_row(short row) {
  const Scanout &s = board.scanout();
  for (int dy = 0; dy < 8; dy++) {
    uint16_t *dst = frame_buffer + dy * ColumnMap::OUT_MAX;
    const int y = row * 8 + dy - TOP_ROW;
    if (y < 0 || y >= Scanout::ROWS) {
      memset(dst, 0, ColumnMap::OUT_MAX * sizeof(uint16_t));
      continue;
    }
    const uint8_t *line = s.frame(shown) + y * Scanout::COLS;
    const uint16_t *pen = pens[(y + Scanout::FIRST_LINE) / Scanout::BAND_LINES];
    clearPads(dst);
    dst += columns.left();
    for (int i = 0; i < columns.count(); i++) {
      const uint16_t x = columns.src(i);
      uint8_t p = williamsPixel(line, x);
      if (!p && columns.merge(i)) {
        p = williamsPixel(line, x + 1);
      }
      dst[i] = pen[p];
    }
  }
}

// columns outside the 239 scaled ones (1 here)
void williams::clearPads(uint16_t *line) {
  const int right = columns.left() + columns.count();
  memset(line, 0, columns.left() * sizeof(uint16_t));
  memset(line + right, 0, (ColumnMap::OUT_MAX - right) * sizeof(uint16_t));
}

// audio core: hold the last value when the ring runs dry
int williams::dacSample() {
  const int v = board.sound().sample();
  if (v != WilliamsSound::EMPTY) {
    lastSample = v;
  }
  return lastSample * DAC_VOLUME;
}

// Williams sound board: 6808 DAC samples produced on the emulation core
void williams::renderAudio(int16_t *out, int n) {
  for (int i = 0; i < n; i++) {
    out[i] = audioClamp(dc.filter(dacSample()), AUDIO_MAX);
  }
}

unsigned char IRAM_ATTR williams::m6809_read(m6809_state *, uint16_t addr) {
  return board.read(addr);
}

unsigned char IRAM_ATTR williams::m6809_read_opcode(m6809_state *, uint16_t addr) {
  return board.read(addr);
}

void IRAM_ATTR williams::m6809_write(m6809_state *, uint16_t addr, uint8_t val) {
  board.write(addr, val);
}
