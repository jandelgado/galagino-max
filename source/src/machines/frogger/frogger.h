#ifndef FROGGER_H
#define FROGGER_H

#include "frogger_dipswitches.h"
#include "../tileaddr.h"
#include "../machineBase.h"

class frogger : public machineBase
{
public:
	frogger() { }
	~frogger();

	signed char machineType() override { return MCH_FROGGER; } 
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	void outZ80(unsigned short Port, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;
	unsigned char inZ80(unsigned short Port) override;

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
	void blit_tile_scroll(short row, signed char col, short scroll);
	virtual const unsigned short *tileRom(unsigned short addr);
	virtual const unsigned short *colorRom(unsigned short addr);
	virtual const uint32_t *spriteRom(unsigned char flags, unsigned char code);
	
	unsigned char snd_irq_state = 0;
	unsigned char snd_command;
	unsigned char snd_filter;
	unsigned long snd_icnt;
	unsigned char snd_ay_port;

};

#endif