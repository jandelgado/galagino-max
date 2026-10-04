#ifndef _1942_H
#define _1942_H

#include "1942_rom1.h"
#include "1942_rom2.h"
#include "1942_rom1_b0.h"
#include "1942_rom1_b1.h"
#include "1942_rom1_b2.h"
#include "1942_dipswitches.h"
#include "1942_logo.h"
#include "1942_character_cmap.h"
#include "1942_spritemap.h"
#include "1942_tilemap.h"
#include "1942_charmap.h"
#include "1942_sprite_cmap.h"
#include "1942_tile_cmap.h"
#include "../tileaddr.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"

class _1942 : public machineBase
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned short[8], COMPRESSED> _1942_charmap;
  Asset<unsigned char, COMPRESSED> _1942_rom_cpu1;
  Asset<unsigned char, COMPRESSED> _1942_rom_cpu1_b0;
  Asset<unsigned char, COMPRESSED> _1942_rom_cpu1_b1;
  Asset<unsigned char, COMPRESSED> _1942_rom_cpu1_b2;
  Asset<unsigned char, COMPRESSED> _1942_rom_cpu2;
  Asset<uint32_t[32], COMPRESSED> _1942_sprites;
  Asset<unsigned short[32][8], COMPRESSED> _1942_colormap_tiles;
  Asset<uint32_t[32], PLAIN> _1942_tilemap;
public:
	_1942();

	signed char machineType() override { return MCH_1942; }
	void start(void) override;
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	static Asset<unsigned short, COMPRESSED> &logo(void);
	bool hasNamcoAudio() override { return false; }

#ifdef LED_PIN
	static void menuLeds(CRGB *leds);
	void gameLeds(CRGB *leds) override;
#endif

protected:
	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;

private:
	void blit_bgtile_row(short row);
	void lsl64(unsigned long *mask, int pix);
	void lsr64(unsigned long *mask, int pix);
	// Cached: hot path reads these per access; data() checks the cache on every call.
	const unsigned char *rom_cpu1_ptr = nullptr;
	const unsigned char *rom_cpu2_ptr = nullptr;
	const unsigned char *rom_cpu1_b0_ptr = nullptr;
	const unsigned char *rom_cpu1_b1_ptr = nullptr;
	const unsigned char *rom_cpu1_b2_ptr = nullptr;

	unsigned char _1942_bank = 0;
	unsigned char _1942_palette = 0;
	unsigned short _1942_scroll = 0;
	unsigned char _1942_sound_latch = 0;
	unsigned char _1942_ay_addr[2];
	char sub_cpu_reset = 1;

	unsigned char last_coin = 0;

	static constexpr uint16_t SPRITE_RAM = 0x2400;      // CPU 0xcc00
	static constexpr uint16_t SPRITE_RAM_SIZE = 0x80;

	// Sprites and bg scroll/palette as of the main CPU's vblank IRQ (RST 10h),
	// handed to the video core via Seqlock (see seqlock.h).
	// prepare_frame()/render read video.*, never live RAM/registers. bg tile
	// RAM stays live: the 512-line plane has 256 hidden lines, so freshly
	// streamed rows are never exposed.
	struct VideoState {
		unsigned char sprite_ram[SPRITE_RAM_SIZE];
		uint16_t scroll;
		uint8_t palette;
	};
	Seqlock<VideoState> vblank;
	VideoState video = {};
};

#endif
