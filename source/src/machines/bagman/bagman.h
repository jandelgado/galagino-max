#ifndef BAGMAN_H
#define BAGMAN_H

#include "bagman_dipswitches.h"
#include "bagman_logo.h"

#include "../tileaddr.h"
#include "../machineBase.h"

class bagman : public machineBase
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> bagman_rom_cpu;
  Asset<uint32_t[128][16], COMPRESSED> bagman_sprites;
  Asset<unsigned short[8], COMPRESSED> bagman_tilemap;
public:
  bagman();

  signed char machineType() override { return MCH_BAGMAN; }
  void start(void) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;
  
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static Asset<unsigned short, COMPRESSED> &logo(void);

#ifdef LED_PIN	
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char s) override;
  virtual const unsigned short *tileRom(unsigned short addr);
  virtual const unsigned short *colorRom(unsigned short addr);
  virtual const uint32_t *spriteRom(unsigned char flags, unsigned char code);
  
private:
  void pitch_w(uint8_t data);
  unsigned char gfxbank;

  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *rom_ptr = nullptr;
};

#endif
