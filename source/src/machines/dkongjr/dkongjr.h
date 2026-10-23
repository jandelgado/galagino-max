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
  Asset<uint32_t[16], COMPRESSED> dkongjr_sprites;
  Asset<unsigned short[8], COMPRESSED> dkongjr_tilemap;
public:
	dkongjr();

	uint8_t machineType() override { return MCH_DKONGJR; }
	void reset() override;
	void start(void) override;
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	unsigned char rdI8048_port(struct i8048_state_S *state, unsigned char port) override;
	unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) override;
	unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) override;

	void prepare_frame(void) override;
	static const Logo &logo(void);

protected:
 	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;

private:
	unsigned char palette_bank = 0; // bank-switching   byte (8 bit) 256 valori massimi  Banco 0: Contiene i tile dall'indice 0 al 255. Banco 1: Contiene i tile dall'indice 256 al 511.
	unsigned char gfx_bank = 0; // bank-switching   byte (8 bit) 256 valori massimi  Banco 0: Contiene i tile dall'indice 0 al 255. Banco 1: Contiene i tile dall'indice 256 al 511.
	unsigned char flip_screen = 0;//penso venga usata per tabletop io non penso di usarla

	// Cached: hot path reads these per access; data() checks the cache on every call.
	const unsigned char *rom_ptr = nullptr;

	// Sound CPU P2 inputs from the main board, active low (MAME
	// dkongjr_audio, vp2 xor 0x70). The sound ROM plays roar and snapjaw.
	static const unsigned char P2_SNAPJAW = 0x10; // 7d06
	static const unsigned char P2_ROAR = 0x20;    // 7d03
	static const unsigned char P2_INPUTS = P2_SNAPJAW | P2_ROAR;
	unsigned char p2_in = P2_INPUTS;
};

#endif
