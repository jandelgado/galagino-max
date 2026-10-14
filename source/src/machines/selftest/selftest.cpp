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

// rainbow: the classic Atari 8-bit effect, a color register written every
// scanline with VCOUNT + RTCLOK. VCOUNT counts line pairs, RTCLOK frames
// (60 per second, NTSC), so the colors move up 2 lines per frame
static constexpr uint32_t ATARI_FRAMES_PER_MS_NUM = 3, ATARI_FRAMES_PER_MS_DEN = 50;

// register value / 2 (GTIA ignores bit 0) to 0xRRGGBB. MAME's atari400.cpp
// NTSC palette, made vivid for the panel: luminance squeezed to 2..10 (no
// pastel tops), saturation x1.4
static const uint32_t ATARI_PALETTE[128] = {
  0x222222, 0x222222, 0x444444, 0x444444, 0x666666, 0x666666, 0x888888, 0xAAAAAA,
  0x3C2900, 0x3C2900, 0x5E4B00, 0x5E4B00, 0x806D00, 0x806D00, 0xBCA200, 0xDFC521,
  0x531700, 0x531700, 0x753900, 0x753900, 0xA25D00, 0xA25D00, 0xD58111, 0xF7A333,
  0x600800, 0x600800, 0x912200, 0x912200, 0xBB401A, 0xBB401A, 0xDD623C, 0xFF845E,
  0x60001E, 0x60001E, 0x960B38, 0x960B38, 0xB82D5A, 0xB82D5A, 0xDA4F7C, 0xFC719E,
  0x540050, 0x540050, 0x880B7F, 0x880B7F, 0xAA2DA1, 0xAA2DA1, 0xCC4FC3, 0xEE71E5,
  0x3D0077, 0x3D0077, 0x600AB1, 0x600AB1, 0x822CD3, 0x822CD3, 0xA44EF5, 0xC976FF,
  0x18009B, 0x18009B, 0x3115CA, 0x3115CA, 0x5337EC, 0x5337EC, 0x7F68FF, 0xA58FFF,
  0x001A93, 0x001A93, 0x0C2DCB, 0x0C2DCB, 0x2E4FED, 0x2E4FED, 0x607CFF, 0x8DA5FF,
  0x002F74, 0x002F74, 0x0050AC, 0x0050AC, 0x1771D9, 0x1771D9, 0x3993FB, 0x77B7FF,
  0x003E49, 0x003E49, 0x006378, 0x006378, 0x1190AB, 0x1190AB, 0x33B2CD, 0x55D4EF,
  0x004718, 0x004718, 0x007043, 0x007043, 0x159F68, 0x159F68, 0x37C18A, 0x59E3AC,
  0x004916, 0x004916, 0x047C0B, 0x047C0B, 0x279F21, 0x279F21, 0x49C143, 0x6BE365,
  0x004D09, 0x004D09, 0x267600, 0x267600, 0x4F9800, 0x4F9800, 0x72C117, 0x94E339,
  0x293900, 0x293900, 0x4C5B00, 0x4C5B00, 0x6E7D00, 0x6E7D00, 0xA1B500, 0xC3D722,
  0x432400, 0x432400, 0x654600, 0x654600, 0x876800, 0x876800, 0xC59A01, 0xE7BC23,
};

static uint32_t rainbow(int32_t y, uint32_t ms) {
  const uint8_t reg = (uint8_t)(y / 2 + ms * ATARI_FRAMES_PER_MS_NUM / ATARI_FRAMES_PER_MS_DEN);
  return ATARI_PALETTE[reg >> 1];
}

// RGB565, byte swapped like the frame buffer
static uint16_t rgb565(uint32_t rgb) {
  const uint16_t c = (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f));
  return (uint16_t)((c >> 8) | (c << 8));
}

// Namco style grid, LINE_PX thick: lines every 16 px plus right and bottom border
static constexpr int32_t LINE_PX = 2;

static bool on_grid(int32_t v, int32_t size) {
  return v % GRID < LINE_PX || v >= size - LINE_PX;
}

// crosshatch, white or one rainbow color per scanline, on black.
// overwrites the strip, so it hides earlier phases
void SelfTest::crosshatch(const Draw &d, GridColors colors) {
  static constexpr uint32_t WHITE = 0xFFFFFF;

  for (uint8_t line = 0; line < ROW_LINES; line++) {
    uint16_t *p = d.frame_buffer + line * WIDTH;
    const int32_t y = d.row * ROW_LINES + line;
    const uint16_t color = rgb565(colors == GRID_WHITE ? WHITE : rainbow(y, d.ms));

    const bool row = on_grid(y, HEIGHT);
    for (int32_t x = 0; x < WIDTH; x++) {
      p[x] = row || on_grid(x, WIDTH) ? color : 0;
    }
  }
}

// jingle: two C64 style arpeggios, chord tones cycled top down once per
// frame, an octave doubled voice under it, pulse width swept. A minor when
// the grid appears, F major an octave lower when it turns to color
static constexpr uint32_t GRID_MS = 1000;      // white grid phase
static constexpr uint32_t RAINBOW_MS = 2000;   // colored grid phase

// Hz to SID frequency units, 1 unit = 0.0587 Hz (PAL)
static constexpr uint16_t sid_freq(double hz) {
  return (uint16_t)(hz * 16777216.0 / 985248.0 + 0.5);
}

namespace note {
static constexpr uint16_t F3 = sid_freq(174.61), A3 = sid_freq(220.00), C4 = sid_freq(261.63);
static constexpr uint16_t F4 = sid_freq(349.23), A4 = sid_freq(440.00), C5 = sid_freq(523.25);
static constexpr uint16_t E5 = sid_freq(659.26), A5 = sid_freq(880.00);
}

static constexpr uint8_t ARP_NOTES = 4;

static const struct {
  uint16_t ms;      // since grid start
  uint16_t length;  // gate on, then released
  uint16_t notes[ARP_NOTES];
} ARPEGGIOS[] = {
  { 0,       500, { note::A5, note::E5, note::C5, note::A4 } },
  { GRID_MS, 700, { note::F4, note::C4, note::A3, note::F3 } },
};

// Sid registers, voice v at v * VOICE_REGS
enum SidReg : uint8_t { FREQ_LO, FREQ_HI, PW_LO, PW_HI, CTRL, AD, SR, VOICE_REGS };
static constexpr uint8_t MODE_VOL = 0x18;
static constexpr uint8_t VOL_MAX = 0x0f;
static constexpr uint8_t CTRL_GATE = 0x01, CTRL_PULSE = 0x40;
static constexpr uint8_t ARP_AD = 0x09;  // attack 2 ms, decay 750 ms
static constexpr uint8_t ARP_SR = 0x48;  // sustain 4, release 300 ms

// pulse width sweep, 12 bit width
static constexpr uint16_t PW_MIN = 0x200;
static constexpr uint16_t PW_RANGE = 0x600;
static constexpr uint16_t PW_STEP = 0x40;  // per frame

enum JingleVoice : uint8_t { VOICE_HIGH, VOICE_LOW, JINGLE_VOICES };

void SelfTest::sidVoice(uint8_t v, uint8_t reg, uint8_t val) {
  sid.write(v * VOICE_REGS + reg, val);
}

void SelfTest::sidFreq(uint8_t v, uint16_t freq) {
  sidVoice(v, FREQ_LO, freq & 0xff);
  sidVoice(v, FREQ_HI, freq >> 8);
}

// gate off at the start
void SelfTest::toneSetup() {
  sid.reset();
  sid.write(MODE_VOL, VOL_MAX);
  for (uint8_t v = 0; v < JINGLE_VOICES; v++) {
    sidVoice(v, AD, ARP_AD);
    sidVoice(v, SR, ARP_SR);
  }
}

// once per frame: next arpeggio note, gate on while one plays at ms since
// grid start
void SelfTest::jingle(uint32_t ms) {
  const uint16_t *notes = nullptr;
  for (const auto &arp : ARPEGGIOS) {
    if (ms >= arp.ms && ms < uint32_t(arp.ms + arp.length)) {
      notes = arp.notes;
    }
  }

  if (!notes) {
    arp_step = 0;  // next arpeggio starts on its top note
    sidVoice(VOICE_HIGH, CTRL, CTRL_PULSE);
    sidVoice(VOICE_LOW, CTRL, CTRL_PULSE);
    return;
  }

  const uint16_t freq = notes[arp_step % ARP_NOTES];
  const uint16_t pw = PW_MIN + arp_step * PW_STEP % PW_RANGE;
  arp_step++;

  sidFreq(VOICE_HIGH, freq);
  sidFreq(VOICE_LOW, freq / 2);
  for (uint8_t v = 0; v < JINGLE_VOICES; v++) {
    sidVoice(v, PW_LO, pw & 0xff);
    sidVoice(v, PW_HI, pw >> 8);
    sidVoice(v, CTRL, CTRL_PULSE | CTRL_GATE);
  }
}

static constexpr uint32_t LEDS_KEEP = 0xFFFFFFFF;  // keep the previous color
static constexpr uint32_t LEDS_RED = 0xFF0000;
static constexpr uint32_t LEDS_GREEN = 0x00FF00;
static constexpr uint32_t LEDS_BLUE = 0x0000FF;
static constexpr uint32_t LEDS_WHITE = 0xFFFFFF;
static constexpr uint32_t LEDS_RAINBOW = 0xFFFFFFFE;  // all the top scanline's rainbow color

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
  { GRID_MS, LEDS_WHITE, [](const SelfTest &, const Draw &d) { crosshatch(d, GRID_WHITE); },
                        [](SelfTest &t, uint32_t ms, uint32_t) { t.jingle(ms); } },
  { RAINBOW_MS, LEDS_RAINBOW, [](const SelfTest &, const Draw &d) { crosshatch(d, GRID_RAINBOW); },
                        [](SelfTest &t, uint32_t ms, uint32_t) { t.jingle(GRID_MS + ms); } },
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
  static constexpr uint8_t ENDLESS_COINS = 2;

  // coin (BUTTON_EXTRA) twice: the last phase runs until reset
  const unsigned int keys = input->buttons_get();
  if (keys & ~last_keys & BUTTON_EXTRA && coins < ENDLESS_COINS) {
    coins++;
  }
  last_keys = keys;

  frame_ms = millis() - start_ms;

  uint32_t phase_start = 0;
  for (phase = 0; phase < PHASE_COUNT; phase++) {
    if (frame_ms < phase_start + PHASES[phase].ms) {
      break;
    }
    phase_start += PHASES[phase].ms;
  }
  if (phase == PHASE_COUNT && coins == ENDLESS_COINS) {
    phase--;
    phase_start -= PHASES[phase].ms;
  }
  phase_ms = frame_ms - phase_start;

  if (phase < PHASE_COUNT && PHASES[phase].tone) {
    PHASES[phase].tone(*this, phase_ms, PHASES[phase].ms);
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
  uint32_t color = 0;

  // last color set by the phases so far
  for (int8_t p = phase < PHASE_COUNT ? phase : -1; p >= 0; p--) {
    if (PHASES[p].led != LEDS_KEEP) {
      color = PHASES[p].led;
      break;
    }
  }

  if (color == LEDS_RAINBOW) {
    color = rainbow(0, phase_ms);
  }
  fill_solid(leds, NUM_LEDS, CRGB(color));
}
#endif
#endif
