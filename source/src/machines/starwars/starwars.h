#ifndef STARWARS_H
#define STARWARS_H

#include "../machineBase.h"
#include "../../emulation/pokey.h"
#include "../../cpus/m6809/m6809.h"
#include "../../emulation/avg.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/speech_ring.h"
#include "../../emulation/tms5220.h"
#include "../../emulation/vector2d.h"
#include "starwars_logo.h"
#include "starwars_dipswitches.h"
#include "starwars_math.h"
#include "starwars_riot.h"
#include "starwars_yoke.h"

// Atari Star Wars, MAME starwars (set 1), atari/starwars.cpp.
// Main M6809 and sound M6809 at 12.096 MHz / 8, AVG vector generator
// (Star Wars variant, color), math board (starwars_math.h), 4 POKEY and
// a 6532 RIOT and a TMS5220 speech chip on the sound board. ROT0,
// 250 x 280, scaled onto 240 columns.
//
//   emulation core                     video core
//   main CPU writes VRAM, GO:          prepare_frame(): go_list ->
//     avg::decode(), map_lines()         lines, raster.begin()
//     publishes go_list -------------> render_row() x 36
//   sound CPU writes POKEY regs
//     -> soundregs -> renderAudio() (audio core)
//   sound CPU drives the TMS5220
//     via RIOT -> speech ---------> renderSpeech() (audio core)
//
// Decoding at GO like the real AVG keeps the vector count off the video
// core, which has no time left beside SPI and audio.
//
// Controls: stick steers the yoke (starwars_yoke.h), FIRE = trigger,
// START = coin + trigger (the trigger starts a game).
class starwars : public machineBase {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> starwars_rom;
  Asset<unsigned char, COMPRESSED> starwars_vrom;
  Asset<unsigned char, COMPRESSED> starwars_srom;
  Asset<unsigned short, COMPRESSED> starwars_mproms;

public:
  starwars();
  signed char machineType() override { return MCH_STARWARS; }
  void renderAudio(int16_t *out, int n) override;
  void start() override;
  void reset() override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  void renderSpeech(int16_t *out, int n);
  const int renderWidth() override { return 240; }
  const int renderBuffer() override { return 240 * 2 * 8; }
  static const Logo &logo() { return starwars_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  // keep in sync with MAX_LINES in romconv/romconv/games/starwars.py
  static const uint16_t MAX_LINES = 2048;

private:
  static const uint16_t VRAM_SIZE = 0x3000; // 0x0000-0x2fff, AVG address 0
  static const uint8_t STRIPS = 36;

  // mapped line, half a vec2d::Line
  struct PackedLine {
    uint16_t y0, y1;
    uint8_t x0, x1, color;
  };
  static_assert(sizeof(PackedLine) == 8, "PackedLine size is part of the Arena budget");

  // display list of the last AVG GO, see seqlock.h
  struct GoList {
    uint16_t count;
    PackedLine lines[MAX_LINES];
  };

  uint8_t main_read(uint16_t a);
  void main_write(uint16_t a, uint8_t v);
  uint8_t sound_read(uint16_t a);
  void sound_write(uint16_t a, uint8_t v);
  void outlatch_write(uint8_t bit, bool v);
  void reset_sound_cpu();
  void install_rom_direct();
  uint8_t in0() const;
  uint8_t in1() const;
  uint8_t riot_pa_in();
  void speech_pins();
  void go();
  bool main_idle() const;
  bool sound_idle(uint32_t until);
  void run_sound(uint32_t until);
  void frame_input();
  uint16_t map_lines(uint16_t count);

  const uint8_t *rom = nullptr, *vrom = nullptr, *srom = nullptr;
  const uint8_t *bank = nullptr; // 0x6000-0x7fff
  uint8_t *math_ram = nullptr;   // 0x5000-0x5fff, Arena

  m6809_state main_cpu, sound_cpu;
  uint32_t main_cycles = 0, sound_cycles = 0; // CPU clock since reset
  uint32_t next_irq = 0;                      // main_cycles of the next 3 kHz / 12 IRQ
  bool main_irq = false;                      // IRQ line, held until acknowledged

  StarWarsMath math;
  Riot6532 riot;
  Tms5220 tms;        // RIOT PA0 /WS, PA1 /RS, PA2 READYQ, PB data
  SpeechRing speech;  // tms -> audio core
  uint8_t sound_latch = 0, main_latch = 0;    // main -> sound, sound -> main
  bool sound_pending = false, main_pending = false;
  uint8_t outlatch = 0;                       // LS259 9L/M Q0-Q7
  uint8_t adc_channel = 0;
  YokeAxis yoke_x, yoke_y;
  uint8_t nvram[0x100];                       // X2212, 4 bit cells

  avg::State avg_state;                // emulation core only
  vec2d::Line *work = nullptr;         // emulation core decode buffer, Arena
  Seqlock<GoList> *go_list = nullptr;  // Arena

  vec2d::Line *lines = nullptr; // video core line list, Arena
  uint16_t *line_next = nullptr, *line_active = nullptr, *strip_head = nullptr;
  vec2d::StripRasterizer raster;
  uint16_t palette[128]; // avg::SW_COLOR_SHIFT color index -> RGB565, byte swapped

private:
  static const int POKEY_SHIFT = 1;
  static const int SPEECH_SHIFT = 6;  // int16 -> +-512, a POKEY here is +-240
  Pokey pokey[4] = {Pokey(Pokey::CLOCKS_1512KHZ), Pokey(Pokey::CLOCKS_1512KHZ),
                    Pokey(Pokey::CLOCKS_1512KHZ), Pokey(Pokey::CLOCKS_1512KHZ)};
};

#endif
