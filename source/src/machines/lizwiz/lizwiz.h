#ifndef LIZWIZ_H
#define LIZWIZ_H

#include "lizwiz_dipswitches.h"
#include "lizwiz_logo.h"
#include "../tileaddr.h"
#include "../pacman/pacman.h"

class lizwiz : public pacman
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> lizwiz_rom;
  Asset<uint32_t[64][16], COMPRESSED> lizwiz_sprites;
  Asset<unsigned short[8], COMPRESSED> lizwiz_tilemap;
public:
  lizwiz();

  signed char machineType() override { return MCH_LIZWIZ; }
  void start(void) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  void outZ80(unsigned short Port, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;

  void run_frame(void) override;
  const signed char *waveRom(unsigned char value) override;
  static Asset<unsigned short, COMPRESSED> &logo(void);  

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  const unsigned short *tileRom(unsigned short addr) override;
  const unsigned short *colorRom(unsigned short addr) override;
  const uint32_t *spriteRom(unsigned char flags, unsigned char code) override;

private:
  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *rom_ptr = nullptr;
};

#endif
