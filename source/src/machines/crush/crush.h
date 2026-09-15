#ifndef crush_H
#define crush_H

#include "crush_dipswitches.h"
#include "crush_logo.h"
#include "../tileaddr.h"
#include "../pacman/pacman.h"

class crush : public pacman
{
public:
  crush() { }
  ~crush();

  signed char machineType() override { return MCH_CRUSH; }
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  void outZ80(unsigned short Port, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;

  void run_frame(void) override;
  const signed char *waveRom(unsigned char value) override;
  static const unsigned short *logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  const unsigned short *tileRom(unsigned short addr) override;
  const unsigned short *colorRom(unsigned short addr) override;
  const uint32_t *spriteRom(unsigned char flags, unsigned char code) override;

private:
  void maketrax_protection_w(uint8_t data);
  uint8_t maketrax_special_port2_r(unsigned short offset);
  uint8_t maketrax_special_port3_r(unsigned short offset);
  uint8_t m_maketrax_counter;
  uint8_t m_maketrax_offset;
  uint8_t m_maketrax_disable_protection;
  unsigned long timerSoundChanged;

};



#endif
