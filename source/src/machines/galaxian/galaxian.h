#ifndef GALAXIAN_H
#define GALAXIAN_H

#include "galaxian_logo.h"
#include "galaxian_dipswitches.h"
#include "../tileaddr.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"

// Starfield: max number of visible stars (LFSR generates ~252)
#define GAL_MAX_STARS 256

class galaxian : public machineBase
{
public:
  galaxian() { }
  ~galaxian();

  signed char machineType() override { return MCH_GALAXIAN; }
  void start(void) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;

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

  void blit_tile_scroll(short row, signed char col, unsigned char scroll);

private:
  // Bullet rendering (8 bullets: 7 enemy shells + 1 player missile)
  short bullet_x[8], bullet_y[8];
  unsigned char bullet_active;  // bitmask of active bullets

  // Starfield
  struct star_entry {
    unsigned char x;       // 0-255 horizontal position (landscape)
    unsigned char y;       // 0-255 vertical position (landscape)
    unsigned short color;  // RGB565 byte-swapped
  };
  star_entry stars[GAL_MAX_STARS];
  int star_count = 0;
  int star_scroll_offset = 0;   // scrolls +1 each frame
  bool stars_enabled = false;
  bool stars_initialized = false;
  void stars_init();

  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *rom_ptr = nullptr;

  // Tiles, object RAM (scroll/color attributes, sprites, bullets) and the
  // stars enable as of the vblank NMI, handed to the video core via Seqlock
  // (see seqlock.h). prepare_frame()/render read video.*, never live RAM.
  // Tile RAM is included: per-row scroll moves the alien formation, a
  // snapshot scroll against live tiles would mix two frames.
  static const uint16_t VRAM_BASE = 0x0800;    // HW 0x5000
  static const uint16_t OBJRAM_BASE = 0x0C00;  // HW 0x5800
  static const uint16_t SPRITE_OFS = 0x40;
  static const uint16_t BULLET_OFS = 0x60;
  struct VideoState {
    unsigned char vram[0x400];
    unsigned char objram[0x80];  // attr 0x00-0x3F, sprites 0x40, bullets 0x60
    unsigned char stars_enabled;
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};
};

#endif
