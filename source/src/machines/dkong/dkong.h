#ifndef DKONG_H
#define DKONG_H

#define DKONG_AUDIO_QUEUE_LEN   16
#define DKONG_AUDIO_QUEUE_MASK (DKONG_AUDIO_QUEUE_LEN-1)

#include "dkong_dipswitches.h"
#include "../tileaddr.h"
#include "../machineBase.h"

class dkong : public machineBase
{
public:
	dkong() { }
	~dkong();

 	void reset() override;
	void start(void) override;
	signed char machineType() override { return MCH_DKONG; }
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char pos) override;
	unsigned char rdI8048_port(struct i8048_state_S *state, unsigned char port) override;
	unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) override;
	unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	static RomData<unsigned short, COMPRESSED> &logo(void);
#ifdef LED_PIN
	static void menuLeds(CRGB *leds);
	void gameLeds(CRGB *leds) override;
#endif

	unsigned char dkong_obuf_toggle = 0;
	unsigned char dkong_audio_transfer_buffer[DKONG_AUDIO_QUEUE_LEN][64];
	unsigned char dkong_audio_rptr = 0, dkong_audio_wptr = 0;
	unsigned short dkong_sample_cnt[6] = { 0,0,0,0,0,0};
	const signed char *dkong_sample_ptr[6];

protected:
 	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;
	void trigger_sound(char snd);
	
	i8048_state_S cpu_8048;
	
	// sound effects register between 8048 and z80
	unsigned char dkong_sfx_index = 0x00;

	// the audio cpu is a mb8884 which in turn is 8048/49 compatible
	unsigned char dkong_audio_assembly_buffer[64];

private:
	// special variables for dkong
	unsigned char colortable_select = 0;

	// Cached: hot path reads these per access; data() checks the cache on every call.
	const unsigned char *rom_cpu1_ptr = nullptr;
};

#endif