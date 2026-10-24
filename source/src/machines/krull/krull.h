#ifndef KRULL_H
#define KRULL_H

#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/twin_stick.h"
#include "../gottlieb/gottlieb_machine.h"
#include "krull_dipswitches.h"
#include "krull_logo.h"

// Krull (Gottlieb 1983), MAME gottlieb/gottlieb.cpp: the board as
// gottliebMachine with program ROM at 1000-2fff and tiles in char RAM.
// Game logic runs every 2nd NMI (the handler sets [9E8h]); between, the
// main thread spins in the idle loop at 0x1581.

#define KRULL_RAM_END  0x1000   // 8088 RAM 0000-0fff
#define KRULL_VRAM     (KRULL_RAM_END + gottliebMachine::VRAM_OFS)
#define KRULL_CHARRAM  (KRULL_RAM_END + gottliebMachine::CHARRAM_OFS)
static_assert(KRULL_RAM_END + gottliebMachine::RAM_TILES_END <= RAMSIZE, "krull memory layout exceeds RAMSIZE");

class krull : public gottliebMachine {
protected:
  Asset<unsigned char, COMPRESSED> krull_main_rom;
  Asset<uint32_t[32], COMPRESSED> krull_sprites;
  Asset<unsigned char, COMPRESSED> krull_levels;
  Asset<unsigned char, COMPRESSED> krull_sound_rom;

public:
  krull();
  uint8_t machineType() override { return MCH_KRULL; }
  void reset() override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void) { return krull_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const int CHAR_TILES = 128;

  // render input latched at vblank, char tiles included: the game rewrites
  // them while they are visible
  struct VideoState {
    GottliebVideoInput in;
    uint32_t tiles[CHAR_TILES][8];      // char RAM as tile column words
  };

  static const Board BOARD;

  uint8_t readDsw(void) override;
  uint8_t readIn1(unsigned int keys) override;
  uint8_t readIn4(unsigned int keys) override;
  void pollInputs(void) override;
  void publishVideo(void) override;

  Seqlock<VideoState> *vblank = nullptr;   // Arena
  VideoState *video = nullptr;             // Arena, render copy

  TwinStick sticks;
  uint16_t stickState = 0;         // sticks.update() of this frame, read by IN4
};

#endif
