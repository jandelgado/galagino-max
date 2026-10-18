#ifndef LOADER_SCREEN_H
#define LOADER_SCREEN_H

#include <cstdint>

class Video;
class Input;

// Download screen of the game loader: green on black, terminal style. Runs
// in switchTo() between two machines (emulation stopped), so it sends whole
// frames itself: 36 strips of the shared frame buffer per redraw.
//
//   LOADING GARDIA
//
//   ERASE
//   WIFI OK
//   GET 3FA1...
//   SHA OK
//   ...
//   108656 OF 108656
//   [##########          ]
class LoaderScreen {
public:
  LoaderScreen(Video &video, uint16_t *frameBuffer, Input &input)
      : video(video), frameBuffer(frameBuffer), input(input) { }

  void begin(const char *game);
  void line(const char *text);
  // redraws at most every PROGRESS_MS
  void progress(uint32_t got, uint32_t total);
  // error line, then waits until a button is pressed
  void fail(const char *text);

private:
  static constexpr uint8_t MAX_LINES = 12;
  static constexpr uint8_t LINE_CHARS = 28;  // 224 / 8 px glyphs
  static constexpr uint32_t PROGRESS_MS = 250;

  void draw();
  void drawBar(uint8_t row, uint16_t y);

  Video &video;
  uint16_t *frameBuffer;
  Input &input;
  char title[LINE_CHARS + 1] = {};
  char lines[MAX_LINES][LINE_CHARS + 1] = {};
  uint8_t lineCount = 0;
  uint32_t got = 0;
  uint32_t total = 0;
  uint32_t lastDraw = 0;
};

#endif
