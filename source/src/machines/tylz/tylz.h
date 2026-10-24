#ifndef TYLZ_H
#define TYLZ_H

#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../gottlieb/gottlieb_machine.h"
#include "tylz_dipswitches.h"
#include "tylz_logo.h"

// Tylz (Mylstar 1984 prototype), MAME gottlieb/gottlieb.cpp: Q*bert's board
// (gottliebMachine, ROM tiles, Votrax) unrotated, program ROM at 8000-ffff.
// The main thread waits for each NMI in "cmp byte [0C11h],0; je $".

#define TYLZ_RAM_END  0x3000   // 8088 RAM 0000-2fff
#define TYLZ_VRAM     (TYLZ_RAM_END + gottliebMachine::VRAM_OFS)
static_assert(TYLZ_RAM_END + gottliebMachine::ROM_TILES_END <= RAMSIZE, "tylz memory layout exceeds RAMSIZE");

class tylz : public gottliebMachine {
protected:
  Asset<unsigned char, COMPRESSED> tylz_main_rom;
  Asset<uint32_t[8], COMPRESSED> tylz_tiles;
  Asset<uint32_t[32], COMPRESSED> tylz_sprites;
  Asset<unsigned char, COMPRESSED> tylz_levels;
  Asset<unsigned char, COMPRESSED> tylz_sound_rom;
  Asset<unsigned char, COMPRESSED> tylz_sc01;

public:
  tylz();
  uint8_t machineType() override { return MCH_TYLZ; }
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void) { return tylz_logo; }
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
