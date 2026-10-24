#ifndef PACMAN_H
#define PACMAN_H

#include "pacman_dipswitches.h"
#include "../tileaddr.h"
#include "../machineBase.h"
#include "../../emulation/namco_wsg.h"

class pacman : public machineBase
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> pacman_rom;
  Asset<uint32_t[16], COMPRESSED> pacman_sprites;
  Asset<unsigned short[8], COMPRESSED> pacman_tilemap;
public:
	pacman();

	uint8_t machineType() override { return MCH_PACMAN; }
	void start(void) override;
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	void outZ80(unsigned short Port, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	void renderAudio(int16_t *out, int n) override { wsg.render(soundregs, waveTable(), out, n); }
	virtual const NamcoWave *waveTable();
	static const Logo &logo(void);
	
#ifdef LED_PIN
	static void menuLeds(CRGB *leds);
	void gameLeds(CRGB *leds) override;
#endif

protected:
	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;
	virtual const unsigned short *tileRom(unsigned short addr);
	virtual const unsigned short *colorRom(unsigned short addr);
	virtual const uint32_t *spriteRom(unsigned char code);

	// Cached: hot path reads these per access; data() checks the cache on every call.
	// Protected: mspacman uses pacman_rom in Pac-Man mode.
	const unsigned char *rom_ptr = nullptr;

protected:
  NamcoWsg wsg;
};

#endif