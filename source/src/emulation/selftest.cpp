// every real arcade machine needs a self test, so 
// does galagino ;-)
#include "selftest.h"

#ifdef BOOT_SELFTEST
#include <Arduino.h>
#include <Esp.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <stdarg.h>
#include "hud.h"

// delay between info lines
static constexpr uint32_t LINE_MS = 150;

static constexpr uint16_t HEIGHT = 288;
static constexpr uint8_t ROW_LINES = 8;
static constexpr uint16_t GRID = 16;

static constexpr uint16_t TITLE_X = 16;
static constexpr uint16_t TITLE_Y = 32;
static constexpr uint16_t MAX_X = TITLE_X + 8 * HUD_GLYPH_SIZE;
static constexpr uint16_t LINES_X = 16;
static constexpr uint16_t LINES_Y = 64;
static constexpr uint16_t LINE_SPACING = 16;

static constexpr uint32_t KB = 1024;
static constexpr uint32_t MB = KB * KB;

// on-chip SRAM, not reported by any runtime API
#if CONFIG_IDF_TARGET_ESP32S3
static constexpr uint32_t SRAM_KB = 512;
#elif CONFIG_IDF_TARGET_ESP32S2
static constexpr uint32_t SRAM_KB = 320;
#elif CONFIG_IDF_TARGET_ESP32C3
static constexpr uint32_t SRAM_KB = 400;
#else
static constexpr uint32_t SRAM_KB = 520;
#endif

// info lines, top to bottom
enum Line : uint8_t { LINE_CHIP, LINE_CPU, LINE_RAM, LINE_ROM, LINE_FLASH, LINE_GAMES, LINE_NUNCHUCK, LINE_COUNT };

static constexpr uint8_t LINE_CHARS = SELFTEST_WIDTH / HUD_GLYPH_SIZE + 1;

static constexpr const char *GAMES_FORMAT = "GAMES  %u";
static constexpr const char *GAMES_DONE_FORMAT = "GAMES  %u OK";

static char lines[LINE_COUNT][LINE_CHARS];
static uint8_t games_total;
static uint32_t start_ms;
static uint32_t frame_ms;
static uint8_t phase;  // index into PHASES, PHASE_COUNT once done

static void set_line(Line line, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(lines[line], LINE_CHARS, fmt, args);
  va_end(args);
  printf("selftest: %s\n", lines[line]);
}

static const char *ok_ng(bool ok) {
  return ok ? "OK" : "NG";
}

// verify the running app image (checksum + SHA-256) in flash
static bool rom_ok() {
  const esp_partition_t *app = esp_ota_get_running_partition();
  if (!app) {
    return false;
  }

  const esp_partition_pos_t pos = { app->address, app->size };
  esp_image_metadata_t meta;
  return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta) == ESP_OK;
}

// one strip row of a phase to draw
struct Draw {
  uint16_t *frame_buffer;
  uint8_t row;
  uint32_t ms;        // time since phase start, keeps running after the phase ends
  uint32_t duration;  // of the phase
};

static void draw_title(const Draw &d) {
  hud_text(d.frame_buffer, SELFTEST_WIDTH, d.row, HUD_FLIP_NONE, TITLE_X, TITLE_Y, HUD_WHITE, "GALAGINO");
}

static void draw_max(const Draw &d) {
  hud_text(d.frame_buffer, SELFTEST_WIDTH, d.row, HUD_FLIP_NONE, MAX_X, TITLE_Y, HUD_RED, "MAX");
}

// one info line per strip row, so a line is drawn only for its row
static_assert(LINES_Y % ROW_LINES == 0 && LINE_SPACING % ROW_LINES == 0 && HUD_GLYPH_SIZE <= ROW_LINES,
              "info lines must align with strip rows");

static void draw_text(const Draw &d, Line line, const char *text) {
  const uint16_t y = LINES_Y + line * LINE_SPACING;
  if (y / ROW_LINES != d.row) {
    return;
  }
  hud_text(d.frame_buffer, SELFTEST_WIDTH, d.row, HUD_FLIP_NONE, LINES_X, y, HUD_WHITE, text);
}

static void draw_line(const Draw &d, Line line) {
  draw_text(d, line, lines[line]);
}

// GAMES line, counts up from 0 during its phase, then OK
static void draw_games(const Draw &d) {
  if (d.ms >= d.duration) {
    draw_line(d, LINE_GAMES);
    return;
  }

  char counting[LINE_CHARS];
  snprintf(counting, LINE_CHARS, GAMES_FORMAT, unsigned(games_total * d.ms / d.duration));
  draw_text(d, LINE_GAMES, counting);
}

// Namco style test grid: lines every 16 px plus right and bottom border.
// clears the strip first, so it hides earlier phases
static void crosshatch(const Draw &d) {
  memset(d.frame_buffer, 0, SELFTEST_WIDTH * ROW_LINES * sizeof(uint16_t));

  for (uint8_t line = 0; line < ROW_LINES; line++) {
    const uint16_t y = d.row * ROW_LINES + line;
    uint16_t *p = d.frame_buffer + line * SELFTEST_WIDTH;

    if (y % GRID == 0 || y == HEIGHT - 1) {
      for (uint16_t x = 0; x < SELFTEST_WIDTH; x++) {
        p[x] = HUD_WHITE;
      }
      continue;
    }

    for (uint16_t x = 0; x < SELFTEST_WIDTH; x += GRID) {
      p[x] = HUD_WHITE;
    }
    p[SELFTEST_WIDTH - 1] = HUD_WHITE;
  }
}

typedef void (*DrawFn)(const Draw &d);

struct Phase {
  uint32_t ms;   // duration
  uint32_t led;  // 0xRRGGBB or LED_KEEP
  DrawFn draw;
};

static constexpr uint32_t LED_KEEP = 0xFFFFFFFF;  // keep the previous color
static constexpr uint32_t LED_RED = 0xFF0000;
static constexpr uint32_t LED_GREEN = 0x00FF00;
static constexpr uint32_t LED_BLUE = 0x0000FF;
static constexpr uint32_t LED_WHITE = 0xFFFFFF;

// boot sequence, played top to bottom. phases stack: each frame draws every
// phase so far. NUNCHUCK stays empty without NUNCHUCK_INPUT
static const Phase PHASES[] = {
  { 1000,    LED_RED,   [](const Draw &d) { draw_title(d); } },
  { 500,     LED_KEEP,  [](const Draw &d) { draw_max(d); } },
  { LINE_MS, LED_GREEN, [](const Draw &d) { draw_line(d, LINE_CHIP); } },
  { 850,     LED_KEEP,  [](const Draw &d) { draw_line(d, LINE_CPU); } },
  { LINE_MS, LED_BLUE,  [](const Draw &d) { draw_line(d, LINE_RAM); } },
  { LINE_MS, LED_KEEP,  [](const Draw &d) { draw_line(d, LINE_ROM); } },
  { LINE_MS, LED_KEEP,  [](const Draw &d) { draw_line(d, LINE_FLASH); } },
  { 750,     LED_KEEP,  [](const Draw &d) { draw_games(d); } },
  { 300,     LED_KEEP,  [](const Draw &d) { draw_line(d, LINE_NUNCHUCK); } },
  { 1000,    LED_WHITE, [](const Draw &d) { crosshatch(d); } },
};
static constexpr uint8_t PHASE_COUNT = sizeof(PHASES) / sizeof(PHASES[0]);

void selftest_begin(Input *input, uint8_t games) {
  set_line(LINE_CHIP, "CHIP   %s REV%u", ESP.getChipModel(), ESP.getChipRevision());
  set_line(LINE_CPU, "CPU    %uMHZ %u CORES", ESP.getCpuFreqMHz(), ESP.getChipCores());
  set_line(LINE_RAM, "RAM    OK (%uK)", SRAM_KB);
  set_line(LINE_ROM, "ROM    %s", ok_ng(rom_ok()));
  set_line(LINE_FLASH, "FLASH  OK (%uMB)", ESP.getFlashChipSize() / MB);
  games_total = games;
  set_line(LINE_GAMES, GAMES_DONE_FORMAT, games);
#ifdef NUNCHUCK_INPUT
  set_line(LINE_NUNCHUCK, "NUNCHUCK %s", ok_ng(input->nunchuckConnected()));
#else
  (void)input;
#endif

  start_ms = millis();
}

bool selftest_tick() {
  frame_ms = millis() - start_ms;

  // current phase
  uint32_t phase_end = 0;
  for (phase = 0; phase < PHASE_COUNT; phase++) {
    phase_end += PHASES[phase].ms;
    if (frame_ms < phase_end) {
      break;
    }
  }

  return phase < PHASE_COUNT;
}

#ifdef LED_PIN
CRGB selftest_led_color() {
  if (phase == PHASE_COUNT) {
    return CRGB::Black;
  }

  // last color set by the phases so far
  for (int8_t p = phase; p >= 0; p--) {
    if (PHASES[p].led != LED_KEEP) {
      return CRGB(PHASES[p].led);
    }
  }
  return CRGB::Black;
}
#endif

void selftest_render_row(uint16_t *frame_buffer, uint8_t row) {
  // every phase so far draws
  uint32_t phase_start = 0;
  for (uint8_t p = 0; p <= phase && p < PHASE_COUNT; p++) {
    PHASES[p].draw({ frame_buffer, row, frame_ms - phase_start, PHASES[p].ms });
    phase_start += PHASES[p].ms;
  }
}
#endif
