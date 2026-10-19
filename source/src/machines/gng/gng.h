#ifndef GNG_H
#define GNG_H

#include "gng_arena_plan.h"
#include "gng_logo.h"
#include "../machineBase.h"
#include "../../emulation/ym2203.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/sidplayer.h"

// Capcom Ghosts'n Goblins (MAME gng, ROM set mm_c_0x). Main 6809 with
// banked ROM, 16x16 scrolling bg with split priority, 128 sprites, 8x8 fg
// text, palette RAM. Music comes from the C64 SID tune, cued by the sound
// latch; effects come from the sound Z80 driving the SSG part of its two
// YM2203 (the FM part plays the arcade music and is dropped).
//
// ROT0 shown upright, 256 x 224 native onto 240 x 224 panel pixels:
//   native x' (8..247)  -> panel column x' - 8 (8 px cropped per side)
//   native y' (0..223)  -> panel line 32 + y'
class gng : public machineBase {
protected:
  // ROM assets: Arena residents unpacked in the ctor (largest first, to
  // fit Arena's two blocks). Main ROM and bg tiles stay in flash: the main
  // ROM compresses worst of all, the bg tiles do not fit.
  gng_rom_asset gng_rom;
  gng_sprites0_asset gng_sprites0;
  gng_sprites1_asset gng_sprites1;
  gng_sprites2_asset gng_sprites2;
  gng_chars_asset gng_chars;
  gng_tile_map_asset gng_tile_map;
  gng_tiles_asset gng_tiles;
  gng_bank_asset gng_bank;
  gng_sound_asset gng_sound;
  gng_sid_asset gng_sid;

public:
  gng();
  signed char machineType() override { return MCH_GNG; }
  void renderAudio(int16_t *out, int n) override;
  const int renderWidth() override { return WIDTH; }
  const int renderBuffer() override { return WIDTH * 2 * 8; }
  void reset() override;
  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  void renderSid(int16_t *out, int n);
  static const Logo &logo() { return gng_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char s) override;

private:
  // memory[] layout
  static const uint16_t MAIN_RAM = 0x0000;     // $0000-$2FFF 1:1
  static const uint16_t SPRITE_RAM = 0x1e00;   // 128 x 4 bytes
  static const uint16_t FG_CODE = 0x2000;      // 32 x 32 rows first
  static const uint16_t FG_ATTR = 0x2400;
  static const uint16_t BG_RAM = 0x2800;       // code $2800, attr $2C00, columns first
  static const uint16_t PALETTE_RAM = 0x3000;  // $3800-$39FF
  static const uint16_t SOUND_RAM = 0x3200;    // sound CPU $C000-$C7FF
  static const uint16_t RAM_END = SOUND_RAM + 0x800;
  static_assert(RAM_END <= RAMSIZE, "gng RAM does not fit memory[]");

  static const uint16_t SPRITE_BYTES = 0x200;
  static const uint16_t BG_BYTES = 0x800;      // code + attr
  static const uint16_t PALETTE_BYTES = 0x200; // RG + B
  static const uint16_t TUNE_RAM_SIZE = 0x1540; // C64 $3800-$4D3F
  // Arena::alloc besides planned assets, read by romconv (arena_plan.py):
  // SID tune RAM (ctor)
  static constexpr uint32_t ARENA_EXTRA[] = { 5440 };
  static_assert(ARENA_EXTRA[0] == TUNE_RAM_SIZE, "update ARENA_EXTRA");
  static_assert(arenaExtrasKey(ARENA_EXTRA, sizeof(ARENA_EXTRA) / sizeof(uint32_t), false) ==
                gng_arena_extras_key, "ARENA_EXTRA changed: rerun romconv");

  static const int WIDTH = 240;
  static const int CROP = 8;          // native columns cropped per side
  static const short FIRST_ROW = 4;   // panel rows 4..31 show the game
  static const short LAST_ROW = 32;
  static const int MAX_FRONT = 16;    // priority tile runs per line

  // cached Asset::data(), read per pixel/tile
  const unsigned char *rom = nullptr;  // CPU $4000-$FFFF
  const unsigned char *bank_rom = nullptr;
  const unsigned char *sound_rom = nullptr;
  const uint32_t (*sprite_banks[3])[32] = {};
  const uint32_t (*tiles)[32] = nullptr;
  const unsigned short *tile_map = nullptr;
  const unsigned short (*chars)[8] = nullptr;
  uint32_t blank_chars[1024 / 32] = {}; // bit set: every pixel transparent
  uint8_t fg_lead = 0; // fg columns drawn 8 px right in this strip (gng_hud.h)

  m6809_state main_cpu;
  uint8_t bank = 0;           // $3E00
  uint8_t scroll[4] = {};     // $3B08-$3B0B: x lo, x hi, y lo, y hi
  // sprite RAM latched at the previous vblank: MAME buffered_spriteram8
  // copies after screen_update, so sprites trail scroll by one frame
  uint8_t sprite_buffer[SPRITE_BYTES] = {};

  // sound CPU (cpu[0])
  uint8_t sound_latch = 0;    // $3A00
  bool sound_reset = true;    // $3D01 low

  // music: written by the emulation core, read by renderSid() (audio core)
  // high byte: sequence, low byte: C64 song, 0 = stopped
  volatile uint16_t music_request = 0;
  uint8_t music_song = 0;     // emulation core
  uint16_t music_seen = 0;    // audio core
  bool sid_on = false;        // audio core, a tune was started

#ifdef LED_PIN
  static const uint8_t LED_FLASH_FRAMES = 60;
  volatile uint8_t led_flash = 0; // set by the emulation core
#endif

  SidPlayer player;
  PsidInfo tune = {};
  bool tune_ok = false;

  // sprite buffer, bg RAM, palette and scroll as of the vblank IRQ (see
  // seqlock.h); fg text RAM stays live (no scroll)
  struct VideoState {
    uint8_t sprites[SPRITE_BYTES];
    uint8_t bg[BG_BYTES];
    uint8_t palette[PALETTE_BYTES];
    uint16_t scroll_x, scroll_y;
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};
  uint16_t colors[256] = {};  // video.palette as RGB565, per frame

  // one bg tile row of a group 1 tile in the current strip: pens 1-5
  // and 7 go in front of sprites
  struct FrontRun {
    uint32_t w[2];            // tile row, flips applied
    int16_t x;                // panel column of the tile's first pixel
    uint8_t line;
    uint8_t pal;              // palette base
  };
  FrontRun front[8 * MAX_FRONT];
  uint8_t front_count = 0;

  void wrLatch(uint8_t value);
  void runMain(int cycles);
  void runSound();
  void blit_bg_line(uint8_t line, int y);
  void blit_front(short row);

private:
  static const uint8_t AY_INC = 4;
  static const uint8_t AY_GAIN = 5;
  Ym2203 ym[2] = {Ym2203(AY_INC, AY_GAIN), Ym2203(AY_INC, AY_GAIN)};
};

#endif
