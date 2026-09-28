#ifndef PACMAN_H
#define PACMAN_H

#include "pacman_dipswitches.h"
#include "../tileaddr.h"
#include "../machineBase.h"

class pacman : public machineBase
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> pacman_rom;
  Asset<uint32_t[64][16], COMPRESSED> pacman_sprites;
  Asset<unsigned short[8], COMPRESSED> pacman_tilemap;
public:
	pacman();

	signed char machineType() override { return MCH_PACMAN; }
	void start(void) override;
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	void outZ80(unsigned short Port, unsigned char Value) override;
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
	virtual const unsigned short *tileRom(unsigned short addr);
	virtual const unsigned short *colorRom(unsigned short addr);
	virtual const uint32_t *spriteRom(unsigned char flags, unsigned char code);

	// Cached: hot path reads these per access; data() checks the cache on every call.
	// Protected: mspacman uses pacman_rom in Pac-Man mode.
	const unsigned char *rom_ptr = nullptr;
};

#endif