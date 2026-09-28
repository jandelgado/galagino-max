#ifndef THEGLOB_H
#define THEGLOB_H

#include "theglob_dipswitches.h"
#include "theglob_logo.h"
#include "../tileaddr.h"
#include "../pacman/pacman.h"

class theglob : public pacman
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> theglob_rom;
  Asset<uint32_t[64][16], COMPRESSED> theglob_sprites;
  Asset<unsigned short[8], COMPRESSED> theglob_tilemap;
public:
  theglob();

  void init(Input *input, unsigned short *framebuffer, sprite_S *spritebuffer, unsigned char *memorybuffer) override;
   void reset() override;

  signed char machineType() override { return MCH_THEGLOB; } 
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  void outZ80(unsigned short Port, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char inZ80(unsigned short Port) override;

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
  uint8_t epos_decryption_w(short offset);
  void epos_decrypt_rom(uint8_t *ROM, uint8_t invert, int offset, int *bs);

  unsigned char *decrypt_rom_buffer;
  unsigned short decrypt_rom_index;
  short m_counter;

#pragma GCC system_header // without #pragma: "warning: 'if constexpr' only available with -std=c++17 or -std=gnu++17"
  template <typename T, typename U> constexpr T BITS(T x, U n) noexcept { 
    return (x >> n) & T(1); 
  }

  template <typename T, typename U, typename... V> constexpr T bitswap(T val, U b, V... c) noexcept {
    if constexpr (sizeof...(c) > 0U)
      return (BITS(val, b) << sizeof...(c)) | bitswap(val, c...);
    else
      return BITS(val, b);
  }

  template <unsigned B, typename T, typename... U> constexpr T bitswap(T val, U... b) noexcept {
    static_assert(sizeof...(b) == B, "wrong number of bits");
    return bitswap(val, b...);
  }

};

#endif
