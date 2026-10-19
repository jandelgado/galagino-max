#ifndef GORF_H
#define GORF_H

#include "gorf_logo.h"
#include "gorf_dipswitches.h"
#include "gorf_video.h"
#include "astrocade_bus.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/speech_ring.h"
#include "../../emulation/dc_block.h"
#include "../../emulation/astrocade_sound.h"
#include "../../emulation/votrax.h"

// Midway Gorf (1981, MAME gorf, Bally Astrocade hardware). Z80 @ 1.79 MHz,
// 320 x 204 2bpp bitmap written through the function generator and the
// pattern board, ROT270. Sound: two Astrocade I/O chips + Votrax SC-01.
//
// Player view: column = MAME line 0..239, 320 game rows merged to 288
// panel rows (gorf_video::buildRowMap).
//
//   emulation core (run_frame, 262 lines)      video core
//   Z80 per line, line/lightpen IRQ,           prepare_frame(): read VideoState
//   votrax per line -> ring           ------>  renderAudio(): chips + ring
//   publish VideoState at frame end            render_row(): bitmap + stars
class gorf : public machineBase
{
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> gorf_rom;
  Asset<unsigned char, COMPRESSED> gorf_sc01;

public:
  gorf();

  signed char machineType() override { return MCH_GORF; }
  const int renderWidth() override { return gorf_video::OUT_W; }
  const int renderBuffer() override { return gorf_video::OUT_W * 2 * 8; }
  void reset() override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char inZ80(unsigned short Port) override;
  void outZ80(unsigned short Port, unsigned char Value) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  void renderAudio(int16_t *out, int n) override;
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  // bitmap hardware, no tiles or sprites
  void blit_tile(short row, char col) override { }
  void blit_sprite(short row, unsigned char s) override { }

private:
  // RAM 0xd000-0xdfff in memory[]
  static const uint16_t RAM_BYTES = 0x1000;
  static_assert(RAM_BYTES <= RAMSIZE, "gorf RAM exceeds RAMSIZE");

  // video input latched at frame end: visible bitmap lines + registers
  struct VideoState {
    uint8_t vram[gorf_video::LINES * gorf_video::BYTES];
    gorf_video::Regs regs;
  };

  // pattern board bus: full program map, writes below 0x4000 hit the funcgen
  struct Bus {
    gorf *m;
    uint8_t rd(uint16_t a) { return m->gorf::rdZ80(a); }
    void wr(uint16_t a, uint8_t v) { m->gorf::wrZ80(a, v); }
  };

  void lineIrq(uint16_t line);
  void runLine(void);
  uint8_t readHandle(uint8_t port);
  void soundOut(uint8_t fn, uint8_t data);
  void drawStars(int gameRow, int gameRow2, uint16_t *dst);
  uint8_t pixelAt(int gameRow, int col) const;

  const uint8_t *rom = nullptr;
  uint8_t *vram = nullptr;                // Arena, 16 KB at 0x4000
  Seqlock<VideoState> *vblank = nullptr;  // Arena
  VideoState *video = nullptr;            // Arena, render copy
  uint16_t *pal = nullptr;                // Arena, 512 entries
  uint16_t *rowSrc = nullptr;             // Arena, OUT_H entries
  uint8_t *rowMerge = nullptr;            // Arena, OUT_H entries
  uint32_t *stars = nullptr;              // Arena, (gameRow << 8) | col, sorted
  uint16_t *starStart = nullptr;          // Arena, first star per game row

  astrocade::FuncGen funcgen;
  astrocade::Pattern pattern;
  gorf_video::Regs regs = {};

  // interrupts (MAME astrocde_v.cpp scanline_callback)
  uint8_t irqEnable = 0, irqVector = 0, irqLine = 0;
  uint8_t pendingVector = 0;
  bool irqPending = false, irqPulse = false;
  bool eiShadow = false;  // EI just ran: IRQs wait one instruction
  uint8_t vFeedback = 0, hFeedback = 0;
  int32_t cycleFrac = 0;  // Q8 CPU cycles owed to the next line
  bool ramWriteEnable = false;

  // outlatch (CD4099): q6 sound switch, q7 lamp
  bool soundSwitch = false, lamp = false;

  AstrocadeSound chip[2];
  VotraxSc01 speech;
  SpeechRingT<2048, 768> ring;
  DcBlock dc;

  uint32_t frame = 0;  // video core: sparkle phase

#ifdef LED_PIN
  uint8_t ledFrame = 0;
#endif
};

#endif
