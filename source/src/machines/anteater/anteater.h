#ifndef anteater_H
#define anteater_H

#include "anteater_dipswitches.h"
#include "anteater_logo.h"
#include "../tileaddr.h"
#include "../frogger/frogger.h"

class anteater : public frogger
{
public:
  anteater() { }
  ~anteater();

 	void reset() override;
  void start(void) override;

  signed char machineType() override { return MCH_ANTEATER; }
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  void outZ80(unsigned short Port, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char inZ80(unsigned short Port) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;

  static RomData<unsigned short, COMPRESSED> &logo(void);

#ifdef LED_PIN	
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char s) override;
  void blit_tile_scroll(short row, signed char col, short scroll);

  virtual const unsigned short *tileRom(unsigned short addr) override;
  virtual const unsigned short *colorRom(unsigned short addr) override;
  virtual const uint32_t *spriteRom(unsigned char flags, unsigned char code) override;

private:
  unsigned char showCustomBackground;
  unsigned char ignoreFireButton;

  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *rom_cpu1_ptr = nullptr;
  const unsigned char *rom_cpu2_ptr = nullptr;
};

#endif
