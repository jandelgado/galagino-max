#ifndef QBERT_H
#define QBERT_H

#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../gottlieb/gottlieb_machine.h"
#include "qbert_dipswitches.h"
#include "qbert_logo.h"

// Q*bert (Gottlieb 1982), MAME gottlieb/gottlieb.cpp: the board as
// gottliebMachine, ROM tiles. The main thread never runs past its frame wait
// (MAME traces: every NMI hits it), so one cpuRun() per frame matches the
// hardware.

#define QBERT_RAM_END  0x3000   // 8088 RAM 0000-2fff
#define QBERT_VRAM     (QBERT_RAM_END + gottliebMachine::VRAM_OFS)
static_assert(QBERT_RAM_END + gottliebMachine::ROM_TILES_END <= RAMSIZE, "qbert memory layout exceeds RAMSIZE");

class qbert : public gottliebMachine {
protected:
  Asset<unsigned char, COMPRESSED> qbert_main_rom;
  Asset<uint32_t[8], COMPRESSED> qbert_tiles;
  Asset<uint32_t[32], COMPRESSED> qbert_sprites;
  Asset<unsigned char, COMPRESSED> qbert_levels;
  Asset<unsigned char, COMPRESSED> qbert_sound_rom;
  Asset<unsigned char, COMPRESSED> qbert_sc01;

public:
  qbert();
  uint8_t machineType() override { return MCH_QBERT; }
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void) { return qbert_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const Board BOARD;

  uint8_t readDsw(void) override;
  uint8_t readIn1(unsigned int keys) override;
  uint8_t readIn4(unsigned int keys) override;
  void publishVideo(void) override;

  const uint32_t (*tiles)[8] = nullptr;
  Seqlock<GottliebVideoInput> *vblank = nullptr;   // Arena
  GottliebVideoInput *video = nullptr;             // Arena, render copy
};

#endif
