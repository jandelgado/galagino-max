#ifndef SWAT_H
#define SWAT_H

#include "../system1/system1.h"
#include "swat_dipswitches.h"

// SWAT (Coreland / Sega 1984), 315-5048 encrypted main CPU: romconv
// stores the data-decrypted ROM as main_rom and the opcode-decrypted
// 0000-7fff as opcodes; M1 fetches there read opcodes
class swat : public system1 {
private:
  Asset<unsigned char, COMPRESSED> opcode_rom;

public:
  swat();

  signed char machineType() override { return MCH_SWAT; }
  static const Logo &logo(void);

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char opPrefixZ80(unsigned short Addr) override;

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  uint8_t dswA() override { return SWAT_SWA; }
  uint8_t dswB() override { return SWAT_SWB; }

private:
  const uint8_t *opcodes;
};

#endif
