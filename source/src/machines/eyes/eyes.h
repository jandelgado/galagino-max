#ifndef EYES_H
#define EYES_H

#include "eyes_dipswitches.h"
#include "eyes_logo.h"
#include "../tileaddr.h"
#include "../pacman/pacman.h"

class eyes : public pacman
{
public:
  eyes() { }
  ~eyes();

  signed char machineType() override { return MCH_EYES; } 
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  void outZ80(unsigned short Port, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;

  void run_frame(void) override;
  const signed char * waveRom(unsigned char value) override;
  static const unsigned short *logo(void);  

#ifdef LED_PIN
       static void menuLeds(CRGB *leds);
       void gameLeds(CRGB *leds) override;
#endif
  
protected:
  const unsigned short *tileRom(unsigned short addr) override;
  const unsigned short *colorRom(unsigned short addr) override;
  const uint32_t *spriteRom(unsigned char flags, unsigned char code) override;

};

#endif
