#ifndef COMMANDO_H
#define COMMANDO_H

#include "commando_logo.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/sidplayer.h"

// Capcom Commando (MAME commando). Main Z80 with encrypted opcodes, 16x16
// scrolling bg, 96 sprites, 8x8 fg text. Music comes from the C64 SID
// tune, cued by the sound latch (commando_music.h). Sound effects come
// from the sound Z80 driving the SSG (AY-3-8910) part of its two YM2203;
// the FM part plays the arcade music and is dropped.
//
// ROT270 onto the 224 x 288 panel (rows 16..271 visible, like 1942):
//   native visible x' (0..255) -> panel row  16 + 255 - x'
//   native visible y' (0..223) -> panel column y'
class commando : public machineBase {
protected:
  // ROM assets: Arena residents unpacked eagerly in the ctor (largest
  // first, to fit Arena's two blocks), tiles stay in flash
  Asset<unsigned char, COMPRESSED> commando_rom;
  Asset<uint32_t[32], COMPRESSED> commando_sprites0;
  Asset<uint32_t[32], COMPRESSED> commando_sprites1;
  Asset<uint32_t[32], COMPRESSED> commando_sprites2;
  Asset<unsigned short[8], COMPRESSED> commando_chars;
  Asset<unsigned short[8], COMPRESSED> commando_cmap_tiles;
  Asset<unsigned short[16], COMPRESSED> commando_cmap_sprites;
  Asset<unsigned short[4], COMPRESSED> commando_cmap_chars;
  Asset<uint32_t[32], PLAIN> commando_tiles;
  Asset<unsigned char, PLAIN> commando_sid;
  Asset<unsigned char, PLAIN> commando_sound;

public:
  commando();
  signed char machineType() override { return MCH_COMMANDO; }
  void reset() override;
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char opPrefixZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  void renderSid(int16_t *out, int n) override;
  static const Logo &logo() { return commando_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char s) override;

private:
  // memory[] layout
  static const uint16_t RAM = 0x0000;         // $E000-$FFFF
  static const uint16_t VRAM = 0x2000;        // $D000-$DFFF
  static const uint16_t FG_CODE = VRAM;       // $D000, 32 x 32 rows first
  static const uint16_t FG_ATTR = VRAM + 0x400;
  static const uint16_t BG_RAM = VRAM + 0x800; // $D800 code, $DC00 attr
  static const uint16_t SPRITE_RAM = RAM + 0x1e00; // $FE00-$FF7F
  static const uint16_t SOUND_RAM = VRAM + 0x1000; // sound CPU $4000-$47FF
  static const uint16_t RAM_END = SOUND_RAM + 0x800;
  static_assert(RAM_END <= RAMSIZE, "commando RAM does not fit memory[]");

  static const uint16_t SPRITE_BYTES = 96 * 4;
  static const uint16_t BG_BYTES = 0x800; // code + attr
  static const uint16_t TUNE_RAM_SIZE = 0x1000; // $5000-$5FFF, see test_sidplayer

  static const int WIDTH = 224;
  static const short FIRST_ROW = 2; // panel rows 2..33 show the game
  static const short LAST_ROW = 34;

  // cached Asset::data(), read per pixel/tile
  const unsigned char *rom = nullptr;
  const unsigned char *sound_rom = nullptr;
  const uint32_t (*sprite_banks[3])[32] = {};
  const uint32_t (*tiles)[32] = nullptr;
  const unsigned short (*chars)[8] = nullptr;
  const unsigned short (*cmap_tiles)[8] = nullptr;
  const unsigned short (*cmap_sprites)[16] = nullptr;
  const unsigned short (*cmap_chars)[4] = nullptr;
  uint32_t blank_chars[1024 / 32] = {}; // bit set: every pixel transparent

  uint8_t scroll[4] = {}; // $C808-$C80B: x lo, x hi, y lo, y hi
  // sprite RAM as latched by the $C806 DMA at vblank start, before the
  // game writes the next frame's sprites (MAME buffered_spriteram8)
  uint8_t sprite_buffer[SPRITE_BYTES] = {};

  // sound CPU (cpu[1])
  uint8_t sound_latch = 0;    // $C800
  bool sound_reset = false;   // $C804 bit 4
  uint8_t ym_addr[2] = {};    // selected register per YM2203

  // music: written by the emulation core, read by renderSid() (audio core)
  // high byte: sequence, low byte: subtune, 0 = stopped
  volatile uint16_t music_request = 0;
  uint8_t music_subtune = 0;     // emulation core
  uint16_t music_seen = 0;       // audio core
  bool sid_on = false;           // audio core

#ifdef LED_PIN
  static const uint8_t LED_FLASH_FRAMES = 60;
  volatile uint8_t led_flash = 0; // set by the emulation core
#endif

  SidPlayer player;
  PsidInfo tune = {};
  bool tune_ok = false;

  // DMA sprite buffer, bg tile RAM and scroll as of the vblank IRQ (see
  // seqlock.h). fg text RAM stays live: it does not scroll.
  struct VideoState {
    uint8_t sprites[SPRITE_BYTES];
    uint8_t bg[BG_BYTES];
    uint16_t scroll_x, scroll_y;
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  void decodeMusic(uint8_t command);
  unsigned char rdSound(unsigned short Addr);
  void wrSound(unsigned short Addr, unsigned char Value);
  void runSound();
  void blit_bg_line(uint16_t *dst, int tx);
  void blit_bg(short row);
};

#endif
