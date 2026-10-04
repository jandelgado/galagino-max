#ifndef AMIDAR_H
#define AMIDAR_H

#include "amidar_logo.h"
#include "amidar_dipswitches.h"
#include "../turtles/turtles.h"

// ============================================================
// Amidar (Konami 1982) — runs on identical hardware to Turtles
// ROM set: amidar  (5 x 4KB main ROMs = 16KB)
// ROM set: amidar1 (4 x 4KB main ROMs = 16KB)
//
// Memory map: identical to Turtles except CPU1_ROM_SIZE = 0x4000 (amidar1)
// Input differences vs Turtles:
//   IN1 bits 1:0: Lives 11=3, 10=4, 01=5, 00=cheat (reversed)
//   IN2 bit 1: Demo Sounds (0=On); bit 2: Bonus Life
//   IN3 (0xB030): Coinage (bits 3:0=CoinA, bits 7:4=CoinB; 0xFF=1C/1C)
// ============================================================

class amidar : public turtles
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> amidar_audio_rom;
  Asset<unsigned char, COMPRESSED> amidar_main_rom;
  Asset<uint32_t[64][16], COMPRESSED> amidar_spritemap;
  Asset<unsigned short[8], COMPRESSED> amidar_tilemap;
public:
  amidar();

  signed char machineType() override { return MCH_AMIDAR; }

  void start(void) override;
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;

  static Asset<unsigned short, COMPRESSED> &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char s) override;

private:
  /*
  static constexpr unsigned short CPU1_ROM_SIZE = 0x4000;  // 4 x 4KB (amidar1 set)
  */
  static constexpr unsigned short CPU1_ROM_SIZE = 0x5000;  // 4 x 4KB (amidar set)
  static constexpr unsigned short CPU2_ROM_SIZE = 0x2000;

  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *rom_main_ptr = nullptr;
  const unsigned char *rom_audio_ptr = nullptr;
};

#endif
