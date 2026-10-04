#ifndef DKONGJR_H
#define DKONGJR_H

#include "dkongjr_logo.h"
#include "../dkong/dkong.h"
#include "../tileaddr.h"
#include "../machineBase.h"

class dkongjr : public dkong
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned short[256][4], COMPRESSED> dkongjr_colormap;
  Asset<unsigned char, COMPRESSED> dkongjr_rom1;
  Asset<unsigned char, COMPRESSED> dkongjr_rom2;
  Asset<uint32_t[128][16], COMPRESSED> dkongjr_sprites;
  Asset<unsigned short[8], COMPRESSED> dkongjr_tilemap;
public:
	dkongjr();

	signed char machineType() override { return MCH_DKONGJR; }
	void start(void) override;
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) override;
	unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) override;

	void prepare_frame(void) override;
	static Asset<unsigned short, COMPRESSED> &logo(void);

protected:
 	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;

private:
	unsigned char palette_bank = 0; // bank-switching   byte (8 bit) 256 valori massimi  Banco 0: Contiene i tile dall'indice 0 al 255. Banco 1: Contiene i tile dall'indice 256 al 511.
	unsigned char gfx_bank = 0; // bank-switching   byte (8 bit) 256 valori massimi  Banco 0: Contiene i tile dall'indice 0 al 255. Banco 1: Contiene i tile dall'indice 256 al 511.
	unsigned char flip_screen = 0;//penso venga usata per tabletop io non penso di usarla

	// Cached: hot path reads these per access; data() checks the cache on every call.
	const unsigned char *rom_ptr = nullptr;
};

#endif
