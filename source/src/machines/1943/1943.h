#ifndef _1943_H
#define _1943_H

#include "1943_logo.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/ym2203fm.h"

// Capcom 1943: The Battle of Midway, MAME set 1943b (bootleg without the
// i8751 protection MCU). Main Z80 6 MHz with 8 banked ROM pages, sound
// Z80 3 MHz with two YM2203: FM through Ym2203Fm, SSG through the AY
// renderer. Video: two 32x32 tile layers scrolling along native x (sea
// bg2 below, clouds bg1 above sprites by pen), 128 sprites, 8x8 text.
//
// ROT270 onto the 224 x 288 panel (rows 16..271 visible, like commando):
//   native visible x' (0..255) -> panel row  16 + 255 - x'
//   native visible y' (0..223) -> panel column y'
class _1943 : public machineBase {
protected:
  // ROM assets; the ctor unpacks the Arena residents in first-fit order:
  // block A tilemap + color maps, block B bg2 + chars
  Asset<unsigned char, COMPRESSED> _1943_tilemap;
  Asset<uint32_t[128], COMPRESSED> _1943_bg2;
  Asset<unsigned short[8], COMPRESSED> _1943_chars;
  Asset<unsigned short[4], COMPRESSED> _1943_cmap_chars;
  Asset<unsigned short[16], COMPRESSED> _1943_cmap_bg1;
  Asset<unsigned short[16], COMPRESSED> _1943_cmap_bg2;
  Asset<unsigned short[16], COMPRESSED> _1943_cmap_sprites;
  Asset<unsigned short[16], COMPRESSED> _1943_pen_masks;
  Asset<unsigned char, PLAIN> _1943_rom;
  Asset<unsigned char, PLAIN> _1943_sound;
  Asset<uint32_t[128], PLAIN> _1943_bg1;
  Asset<uint32_t[32], PLAIN> _1943_sprites;

public:
  _1943();
  signed char machineType() override { return MCH_1943; }
  void reset() override;
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  void renderFm(int16_t *out, int n) override;
  static const Logo &logo() { return _1943_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  // memory[] layout
  static const uint16_t RAM = 0x0000;        // $E000-$EFFF work RAM
  static const uint16_t SPRITE_RAM = 0x1000; // $F000-$FFFF, 128 x 32 bytes
  static const uint16_t FG_CODE = 0x2000;    // $D000-$D3FF
  static const uint16_t FG_ATTR = 0x2400;    // $D400-$D7FF
  static const uint16_t SOUND_RAM = 0x2800;  // sound CPU $C000-$C7FF
  static const uint16_t RAM_END = 0x3000;
  static_assert(RAM_END <= RAMSIZE, "1943 RAM does not fit memory[]");

  static const uint8_t SPRITES = 128;
  static const int WIDTH = 224;
  static const short FIRST_ROW = 2; // panel rows 2..33 show the game
  static const short LAST_ROW = 34;
  static const int LINE_WORDS = WIDTH / 32; // 1 bit per pixel of a line

  // cached Asset::data()
  const unsigned char *rom = nullptr;
  const unsigned char *sound_rom = nullptr;
  const unsigned char *tilemap = nullptr;
  const uint32_t (*bg1_tiles)[128] = nullptr;
  const uint32_t (*bg2_tiles)[128] = nullptr;
  const uint32_t (*sprite_gfx)[32] = nullptr;
  const unsigned short (*chars)[8] = nullptr;
  const unsigned short (*cmap_chars)[4] = nullptr;
  const unsigned short (*cmap_bg1)[16] = nullptr;
  const unsigned short (*cmap_bg2)[16] = nullptr;
  const unsigned short (*cmap_sprites)[16] = nullptr;
  const unsigned short *bg1_clear = nullptr;  // per color: transparent pens
  const unsigned short *spr_behind = nullptr; // per color: pens behind bg1
  uint32_t blank_chars[2048 / 32] = {};       // bit set: every pixel clear

  // main CPU latches
  uint8_t c804 = 0;     // bank, sound reset, chars on
  uint8_t d806 = 0;     // bg1, bg2, sprites on
  uint8_t scroll[5] = {}; // $D800-$D804
  uint8_t sound_latch = 0;
  bool irq_pending = false; // vblank IRQ held until the main CPU enables
#ifdef LED_PIN
  static const uint8_t LED_FLASH_FRAMES = 60;
  volatile uint8_t led_flash = 0; // set by the emulation core
#endif

  // sound CPU (cpu[1])
  bool sound_reset = false;
  uint8_t ym_addr[2] = {};
  Ym2203Fm fm[2];

  // sprite RAM bytes 0-3, scroll and enables as of the vblank IRQ (see
  // seqlock.h); fg text RAM stays live (not scrolled)
  struct VideoState {
    uint8_t sprites[SPRITES][4];
    uint16_t bg1_x, bg2_x;
    uint8_t bg1_y;
    uint8_t c804, d806;
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  // prepare_frame(): sprites inside the visible area, MAME order (first wins)
  uint8_t spr_list[SPRITES];
  uint8_t spr_count = 0;

  // per strip line: bg1 opaque / sprite pixel drawn, 1 bit per column
  uint32_t bg1_mask[8][LINE_WORDS];
  uint32_t drawn[8][LINE_WORDS];

  unsigned char rdSound(unsigned short Addr);
  void wrSound(unsigned short Addr, unsigned char Value);
  unsigned char rdInput(unsigned short Addr);
  void runSound();
  void latchVideo();
  void blitBg2Line(uint16_t *dst, int vx);
  void blitBg1Line(uint16_t *dst, int vx, uint32_t *mask);
  void blitSprites(short row);
  void blitChar(short row, int col);
};

#endif
