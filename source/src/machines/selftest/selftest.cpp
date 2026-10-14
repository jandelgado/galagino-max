// every real arcade machine needs a self test, so
// does galagino ;-)
#include "selftest.h"

#ifdef BOOT_SELFTEST
#include <Arduino.h>
#include <Esp.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <stdarg.h>
#include "../../emulation/hud.h"

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

static constexpr const char *GAMES_FORMAT = "GAMES  %u";
static constexpr const char *GAMES_DONE_FORMAT = "GAMES  %u OK";

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

void SelfTest::setLine(Line line, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(lines[line], LINE_CHARS, fmt, args);
  va_end(args);
  printf("selftest: %s\n", lines[line]);
}

void SelfTest::drawTitle(const Draw &d) {
  hud_text(d.frame_buffer, WIDTH, d.row, HUD_FLIP_NONE, TITLE_X, TITLE_Y, HUD_WHITE, "GALAGINO");
}

void SelfTest::drawMax(const Draw &d) {
  hud_text(d.frame_buffer, WIDTH, d.row, HUD_FLIP_NONE, MAX_X, TITLE_Y, HUD_RED, "MAX");
}

// one info line per strip row, so a line is drawn only for its row
static_assert(LINES_Y % ROW_LINES == 0 && LINE_SPACING % ROW_LINES == 0 && HUD_GLYPH_SIZE <= ROW_LINES,
              "info lines must align with strip rows");

void SelfTest::drawText(const Draw &d, Line line, const char *text) {
  const uint16_t y = LINES_Y + line * LINE_SPACING;
  if (y / ROW_LINES != d.row) {
    return;
  }
  hud_text(d.frame_buffer, WIDTH, d.row, HUD_FLIP_NONE, LINES_X, y, HUD_WHITE, text);
}

void SelfTest::drawLine(const Draw &d, Line line) const {
  drawText(d, line, lines[line]);
}

// GAMES line, counts up from 0 during its phase, then OK
void SelfTest::drawGames(const Draw &d) const {
  if (d.ms >= d.duration) {
    drawLine(d, LINE_GAMES);
    return;
  }

  char counting[LINE_CHARS];
  snprintf(counting, LINE_CHARS, GAMES_FORMAT, unsigned(games_total * d.ms / d.duration));
  drawText(d, LINE_GAMES, counting);
}

// anaglyph: two grid copies, red moved up-left, green moved down-right,
// fading from white. overlaps stay light (red | green)
static constexpr uint32_t FADE_ONE = 256;
static constexpr int32_t ANAGLYPH_SHIFT = 2;  // px per copy at full fade
static constexpr uint8_t RED[3] = { 31, 0, 0 };     // RGB565 components
static constexpr uint8_t GREEN[3] = { 0, 63, 14 };  // teal, as 3D glasses
static constexpr uint8_t WHITE[3] = { 31, 63, 31 };

// RGB565, byte swapped like the frame buffer
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t c = (uint16_t)((r << 11) | (g << 5) | b);
  return (uint16_t)((c >> 8) | (c << 8));
}

// white faded towards `to` by fade (0..FADE_ONE)
static uint16_t fade_white(const uint8_t to[3], uint32_t fade) {
  uint8_t c[3];
  for (uint8_t i = 0; i < 3; i++) {
    c[i] = (uint8_t)(WHITE[i] - (WHITE[i] - to[i]) * fade / FADE_ONE);
  }
  return rgb565(c[0], c[1], c[2]);
}

// OR screen line y of a grid copy moved up-left by `shift` (down-right if
// negative) into p, `width` pixels. Namco style grid: lines every 16 px
// plus right and bottom border
static void grid_line(uint16_t *p, int32_t width, int32_t y, int32_t shift, uint16_t color) {
  const int32_t gy = y + shift;  // grid line drawn at screen y
  if (gy < 0 || gy >= HEIGHT) {
    return;
  }

  if (gy % GRID == 0 || gy == HEIGHT - 1) {
    const int32_t from = shift < 0 ? -shift : 0;
    const int32_t to = shift > 0 ? width - shift : width;
    for (int32_t x = from; x < to; x++) {
      p[x] |= color;
    }
    return;
  }

  auto column = [&](int32_t gx) {
    const int32_t x = gx - shift;
    if (x >= 0 && x < width) {
      p[x] |= color;
    }
  };
  for (int32_t gx = 0; gx < width; gx += GRID) {
    column(gx);
  }
  column(width - 1);
}

// crosshatch split into anaglyph copies by fade (0 = white grid).
// clears the strip first, so it hides earlier phases
void SelfTest::crosshatch(const Draw &d, uint32_t fade) {
  memset(d.frame_buffer, 0, WIDTH * ROW_LINES * sizeof(uint16_t));

  const int32_t shift = (ANAGLYPH_SHIFT * fade + FADE_ONE / 2) / FADE_ONE;
  const uint16_t red = fade_white(RED, fade);
  const uint16_t green = fade_white(GREEN, fade);

  for (uint8_t line = 0; line < ROW_LINES; line++) {
    uint16_t *p = d.frame_buffer + line * WIDTH;
    const int32_t y = d.row * ROW_LINES + line;
    grid_line(p, WIDTH, y, shift, red);
    grid_line(p, WIDTH, y, -shift, green);
  }
}

void SelfTest::anaglyph(const Draw &d) {
  crosshatch(d, d.ms < d.duration ? d.ms * FADE_ONE / d.duration : FADE_ONE);
}

// tone: Commando.sid subtune 11 pulse voice, doubled. The second voice
// detunes while the grid splits, its beat speeds up from 0 to ~6 Hz.
// 1 SID frequency unit = 0.0587 Hz (PAL)
static constexpr uint16_t TONE_FREQ = 0x0700;   // 105 Hz
static constexpr uint16_t TONE_DETUNE = 100;    // at phase end
static constexpr uint32_t TONE_RELEASE_MS = 300; // SR release 8
static constexpr uint8_t TONE_SR = 0xf8;         // full sustain, release 8

// Sid registers, voice v at v * VOICE_REGS
enum SidReg : uint8_t { FREQ_LO, FREQ_HI, PW_LO, PW_HI, CTRL, AD, SR, VOICE_REGS };
static constexpr uint8_t MODE_VOL = 0x18;
static constexpr uint8_t VOL_MAX = 0x0f;
static constexpr uint8_t CTRL_GATE = 0x01, CTRL_PULSE = 0x40;

void SelfTest::sidVoice(uint8_t v, uint8_t reg, uint8_t val) {
  sid.write(v * VOICE_REGS + reg, val);
}

void SelfTest::sidFreq(uint8_t v, uint16_t freq) {
  sidVoice(v, FREQ_LO, freq & 0xff);
  sidVoice(v, FREQ_HI, freq >> 8);
}

// pulse widths of subtune 11, gate off at the start
void SelfTest::toneSetup() {
  static const uint16_t PULSE_WIDTH[2] = { 0x280, 0x400 };

  sid.reset();
  sid.write(MODE_VOL, VOL_MAX);
  for (uint8_t v = 0; v < 2; v++) {
    sidFreq(v, TONE_FREQ);
    sidVoice(v, PW_LO, PULSE_WIDTH[v] & 0xff);
    sidVoice(v, PW_HI, PULSE_WIDTH[v] >> 8);
    sidVoice(v, AD, 0x00);
    sidVoice(v, SR, TONE_SR);
  }
}

// gate on at phase start, released to end silent with the phase
void SelfTest::anaglyphTone(uint32_t ms, uint32_t duration) {
  sidFreq(1, TONE_FREQ + TONE_DETUNE * ms / duration);

  const bool gate = ms + TONE_RELEASE_MS < duration;
  const uint8_t ctrl = gate ? CTRL_PULSE | CTRL_GATE : CTRL_PULSE;
  sidVoice(0, CTRL, ctrl);
  sidVoice(1, CTRL, ctrl);
}

static constexpr uint32_t LEDS_KEEP = 0xFFFFFFFF;  // keep the previous color
static constexpr uint32_t LEDS_RED = 0xFF0000;
static constexpr uint32_t LEDS_GREEN = 0x00FF00;
static constexpr uint32_t LEDS_BLUE = 0x0000FF;
static constexpr uint32_t LEDS_WHITE = 0xFFFFFF;

// boot sequence, played top to bottom. phases stack: each frame draws every
// phase so far. NUNCHUCK stays empty without NUNCHUCK_INPUT
const SelfTest::Phase SelfTest::PHASES[] = {
  { 1000,    LEDS_RED,   [](const SelfTest &, const Draw &d) { drawTitle(d); } },
  { 500,     LEDS_KEEP,  [](const SelfTest &, const Draw &d) { drawMax(d); } },
  { LINE_MS, LEDS_GREEN, [](const SelfTest &t, const Draw &d) { t.drawLine(d, LINE_CHIP); } },
  { 850,     LEDS_KEEP,  [](const SelfTest &t, const Draw &d) { t.drawLine(d, LINE_CPU); } },
  { LINE_MS, LEDS_BLUE,  [](const SelfTest &t, const Draw &d) { t.drawLine(d, LINE_RAM); } },
  { LINE_MS, LEDS_KEEP,  [](const SelfTest &t, const Draw &d) { t.drawLine(d, LINE_ROM); } },
  { LINE_MS, LEDS_KEEP,  [](const SelfTest &t, const Draw &d) { t.drawLine(d, LINE_FLASH); } },
  { 750,     LEDS_KEEP,  [](const SelfTest &t, const Draw &d) { t.drawGames(d); } },
  { 300,     LEDS_KEEP,  [](const SelfTest &t, const Draw &d) { t.drawLine(d, LINE_NUNCHUCK); } },
  { 1000,    LEDS_WHITE, [](const SelfTest &, const Draw &d) { crosshatch(d, 0); } },
  { 2000,    LEDS_KEEP,  [](const SelfTest &, const Draw &d) { anaglyph(d); },
                        [](SelfTest &t, uint32_t ms, uint32_t duration) { t.anaglyphTone(ms, duration); } },
};
const uint8_t SelfTest::PHASE_COUNT = sizeof(PHASES) / sizeof(PHASES[0]);

SelfTest::SelfTest(Input *input, uint8_t games) : games_total(games) {
  setLine(LINE_CHIP, "CHIP   %s REV%u", ESP.getChipModel(), ESP.getChipRevision());
  setLine(LINE_CPU, "CPU    %uMHZ %u CORES", ESP.getCpuFreqMHz(), ESP.getChipCores());
  setLine(LINE_RAM, "RAM    OK (%uK)", SRAM_KB);
  setLine(LINE_ROM, "ROM    %s", ok_ng(rom_ok()));
  setLine(LINE_FLASH, "FLASH  OK (%uMB)", ESP.getFlashChipSize() / MB);
  setLine(LINE_GAMES, GAMES_DONE_FORMAT, games);
#ifdef NUNCHUCK_INPUT
  setLine(LINE_NUNCHUCK, "NUNCHUCK %s", ok_ng(input->nunchuckConnected()));
#else
  (void)input;
#endif

  toneSetup();
  start_ms = millis();
}

// no CPU to boot: sync to vblank right away
void SelfTest::start() {
  game_started = 1;
}

bool SelfTest::done() const {
  return phase == PHASE_COUNT;
}

// advance to the phase of this frame, update its tone
void SelfTest::prepare_frame() {
  frame_ms = millis() - start_ms;

  uint32_t phase_start = 0;
  for (phase = 0; phase < PHASE_COUNT; phase++) {
    if (frame_ms < phase_start + PHASES[phase].ms) {
      break;
    }
    phase_start += PHASES[phase].ms;
  }

  if (phase < PHASE_COUNT && PHASES[phase].tone) {
    PHASES[phase].tone(*this, frame_ms - phase_start, PHASES[phase].ms);
  }
}

void SelfTest::render_row(short row) {
  // every phase so far draws
  uint32_t phase_start = 0;
  for (uint8_t p = 0; p <= phase && p < PHASE_COUNT; p++) {
    PHASES[p].draw(*this, { frame_buffer, (uint8_t)row, frame_ms - phase_start, PHASES[p].ms });
    phase_start += PHASES[p].ms;
  }
}

void SelfTest::renderSid(int16_t *out, int n) {
  sid.render(out, n);
}

#ifdef LED_PIN
void SelfTest::gameLeds(CRGB *leds) {
  CRGB color = CRGB::Black;

  // last color set by the phases so far
  for (int8_t p = phase < PHASE_COUNT ? phase : -1; p >= 0; p--) {
    if (PHASES[p].led != LEDS_KEEP) {
      color = CRGB(PHASES[p].led);
      break;
    }
  }
  fill_solid(leds, NUM_LEDS, color);
}
#endif
#endif
