#ifndef MRDO_H
#define MRDO_H

#include "mrdo_dipswitches.h"
#include "mrdo_logo.h"
#include "../tileaddr.h"
#include "../machineBase.h"

#define SPRITE_FLIP_X 0x01
#define SPRITE_FLIP_Y 0x02

class mrdo : public machineBase
{
public:
  mrdo() { }
  ~mrdo();

  void reset() override;
  signed char machineType() override { return MCH_MRDO; }
  signed char videoFlipY() override { return 1; }
  signed char useVideoHalfRate() override { return 1; } 

  unsigned char opZ80(unsigned short Addr) override; 
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static RomData<unsigned short, COMPRESSED> &logo(void);

#ifdef LED_PIN  
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  void blit_tile_bg(short logical_row);
  void blit_tile_fg(short row, char col);
  void blit_sprite(short row, unsigned char s_idx) override;

private:
  unsigned char protection_r();
  void render_background_strip(short screen_strip_row);
  void SN76489_Write_2chip(int chip, unsigned char data);

  unsigned char flipscreen_w = 0; // 0 = normale, 1 = flip attivo
  unsigned char scrollx_w = 0;
  unsigned char scrolly_w = 0;

  unsigned char ignoreFireButton;
  int sn_last_register[2];

};

#endif
