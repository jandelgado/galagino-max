#ifdef ASSETS_REMOTE
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include "../emulation/hud.h"
#include "../emulation/input.h"
#include "../emulation/video.h"
#include "loader_screen.h"

static constexpr uint16_t WIDTH = 224;
static constexpr uint8_t ROWS = 36;
static constexpr uint8_t ROW_LINES = 8;
static constexpr uint16_t MARGIN = 8;
static constexpr uint16_t TITLE_Y = 16;
static constexpr uint16_t LINES_Y = 40;
static constexpr uint16_t LINE_STEP = 12;
static constexpr uint16_t COUNT_Y = 240;
static constexpr uint16_t BAR_Y = 256;
static constexpr uint16_t BAR_H = 8;

void LoaderScreen::begin(const char *game) {
  snprintf(title, sizeof(title), "LOADING %s", game);
  lineCount = 0;
  got = total = 0;
  draw();
}

// scrolls up when full
void LoaderScreen::line(const char *text) {
  if (lineCount == MAX_LINES) {
    memmove(lines[0], lines[1], sizeof(lines[0]) * (MAX_LINES - 1));
    lineCount--;
  }
  snprintf(lines[lineCount++], LINE_CHARS + 1, "%s", text);
  draw();
}

// start (empty bar) and end always drawn, between at most every PROGRESS_MS
void LoaderScreen::progress(uint32_t gotBytes, uint32_t totalBytes) {
  const bool edge = gotBytes == 0 || gotBytes == totalBytes || totalBytes != total;
  got = gotBytes;
  total = totalBytes;
  if (!edge && millis() - lastDraw < PROGRESS_MS) {
    return;
  }
  draw();
}

void LoaderScreen::fail(const char *text) {
  char msg[LINE_CHARS + 1];
  snprintf(msg, sizeof(msg), "ERROR %s", text);
  line(msg);
  line("PRESS ANY BUTTON");

  // release first: the button that started the game may still be down
  while (input.buttons_get()) {
    vTaskDelay(20);
  }
  while (!input.buttons_get()) {
    vTaskDelay(20);
  }
}

// [####      ] filled by got / total
void LoaderScreen::drawBar(uint8_t row, uint16_t y) {
  const uint16_t top = row * ROW_LINES;
  if (!total || top + ROW_LINES <= y || top >= y + BAR_H) {
    return;
  }
  const uint16_t width = WIDTH - 2 * MARGIN;
  const uint16_t fill = (uint16_t)((uint64_t)width * got / total);
  for (uint16_t line = top; line < top + ROW_LINES; line++) {
    if (line < y || line >= y + BAR_H) {
      continue;
    }
    uint16_t *p = frameBuffer + (line - top) * WIDTH + MARGIN;
    const bool edge = line == y || line == y + BAR_H - 1;
    for (uint16_t x = 0; x < width; x++) {
      p[x] = (x < fill || edge || x == 0 || x == width - 1) ? HUD_GREEN : 0;
    }
  }
}

void LoaderScreen::draw() {
  char count[LINE_CHARS + 1] = {};
  if (total) {
    snprintf(count, sizeof(count), "%u OF %u", (unsigned)got, (unsigned)total);
  }

  video.setViewport(WIDTH);
  for (uint8_t row = 0; row < ROWS; row++) {
    memset(frameBuffer, 0, WIDTH * ROW_LINES * sizeof(uint16_t));
    hud_text(frameBuffer, WIDTH, row, HUD_FLIP_NONE, MARGIN, TITLE_Y, HUD_GREEN, title);
    for (uint8_t i = 0; i < lineCount; i++) {
      hud_text(frameBuffer, WIDTH, row, HUD_FLIP_NONE, MARGIN, LINES_Y + i * LINE_STEP, HUD_GREEN, lines[i]);
    }
    hud_text(frameBuffer, WIDTH, row, HUD_FLIP_NONE, MARGIN, COUNT_Y, HUD_GREEN, count);
    drawBar(row, BAR_Y);
    video.write(frameBuffer, WIDTH * ROW_LINES);
  }
  lastDraw = millis();
}
#endif
