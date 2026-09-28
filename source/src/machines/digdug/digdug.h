#ifndef DIGDUG_H
#define DIGDUG_H

#include "digdug_rom1.h"
#include "digdug_rom2.h"
#include "digdug_rom3.h"
#include "digdug_dipswitches.h"
#include "digdug_logo.h"
#include "digdug_spritemap.h"
#include "digdug_tilemap.h"
#include "digdug_pftiles.h"
#include "digdug_cmap_tiles.h"
#include "digdug_cmap_sprites.h"
#include "digdug_cmap.h"
#include "digdug_playfield.h"
#include "digdug_wavetable.h"
#include "../tileaddr.h"
#include "../machineBase.h"

#define NAMCO_NMI_DELAY  30  // 10 results in errors

class digdug : public machineBase
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned short[8], COMPRESSED> digdug_pftiles;
  Asset<unsigned char, COMPRESSED> digdug_playfield;
  Asset<unsigned char, COMPRESSED> digdug_rom_cpu1;
  Asset<unsigned char, COMPRESSED> digdug_rom_cpu2;
  Asset<unsigned char, COMPRESSED> digdug_rom_cpu3;
  Asset<uint32_t[256][16], COMPRESSED> digdug_sprites;
  Asset<unsigned short[8], COMPRESSED> digdug_tilemap;
public:
	digdug()
	  : digdug_pftiles(digdug_pftiles_blob),
	    digdug_playfield(digdug_playfield_blob),
	    digdug_rom_cpu1(digdug_rom_cpu1_blob),
	    digdug_rom_cpu2(digdug_rom_cpu2_blob),
	    digdug_rom_cpu3(digdug_rom_cpu3_blob),
	    digdug_sprites(digdug_sprites_blob),
	    digdug_tilemap(digdug_tilemap_blob) { }

	void reset() override;
	void start(void) override;
	signed char machineType() override { return MCH_DIGDUG; }

	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	
	const signed char *waveRom(unsigned char value) override;
	static Asset<unsigned short, COMPRESSED> &logo(void);
	bool hasNamcoAudio() override { return true; }
#ifdef LED_PIN
	static void menuLeds(CRGB *leds);
	void gameLeds(CRGB *leds) override;
#endif
protected:
	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;

private:
	unsigned char keymask_d[3] = { 0x00, 0x00, 0x00};	
	unsigned char namco_command = 0;
	unsigned char namco_mode = 0;
	unsigned char namco_nmi_counter = 0;
	unsigned char namco_credit = 0x00;
	unsigned char digdug_video_latch;
	char sub_cpu_reset = 1;

	// Cached: hot path reads these per access; data() checks the cache on every call.
	const unsigned char *rom_ptr[3] = { nullptr, nullptr, nullptr };
};

#endif