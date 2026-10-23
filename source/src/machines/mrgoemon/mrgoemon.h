#ifndef MRGOEMON_H
#define MRGOEMON_H

#include "../gberet/gberet.h"
#include "mrgoemon_arena_plan.h"

// Mr. Goemon (Konami 1986): gberet board, f800-ffff banked ROM window.
class mrgoemon : public gberet {
public:
  mrgoemon();

  signed char machineType() override { return MCH_MRGOEMON; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

  unsigned char rdZ80(unsigned short Addr) override;
  void          wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;
  void reset() override;

private:
  // ROM offset of the f800 window: 0xc000 + bank * 0x800
  uint16_t bank_base = 0xc000;
};

#endif
