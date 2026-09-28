#ifndef STARFORCE_H
#define STARFORCE_H

#include "../tileaddr.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"

// Offsets
#define STARFORCE_GENERAL_RAM   	0x0000	//1000
#define STARFORCE_FG_VIDEO_RAM  	0x1000	//400
#define STARFORCE_FG_COLOR_RAM  	0x1400	//400
#define STARFORCE_SPRITE_RAM  		0x1800	//80
#define STARFORCE_PALETTE_RAM  		0x1880	//200
#define STARFORCE_HW_CONTROL_RAM  	0x1A80	//40
#define STARFORCE_BG3_VIDEO_RAM  	0x1AC0	//800
#define STARFORCE_BG2_VIDEO_RAM  	0x22C0	//800
#define STARFORCE_BG1_VIDEO_RAM  	0x2AC0	//800
#define STARFORCE_RADAR_RAM  		0x32C0	//400
#define STARFORCE_SOUND_RAM  		0x36C0	//400

class starforce : public machineBase
{
public:
	starforce();
	~starforce();

	signed char machineType() override { return MCH_STARFORCE; }
	signed char useVideoHalfRate() override { return 1; }

	unsigned char opZ80(unsigned short Addr) override; 
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char inZ80(unsigned short Port) override; 
	void outZ80(unsigned short Port, unsigned char Value) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	static RomData<unsigned short, COMPRESSED> &logo(void);

#ifdef LED_PIN
	static void menuLeds(CRGB *leds);
	void gameLeds(CRGB *leds) override;
#endif

protected:
	// ROM assets of one game on this board (starforce, baluba)
	struct Roms {
		RomData<unsigned char, COMPRESSED> &main_cpu;
		RomData<unsigned char, COMPRESSED> &sub_cpu;
		RomData<uint32_t[8], COMPRESSED> &fg;
		RomData<uint32_t[16][2], COMPRESSED> &bg1;
		RomData<uint32_t[16][2], COMPRESSED> &bg2;
		RomData<uint32_t[16][2], COMPRESSED> &bg3;
		RomData<uint32_t[16][2], PLAIN> &sprites_16x16;
		RomData<uint32_t[32][4], PLAIN> &sprites_32x32;
	};
	explicit starforce(const Roms &roms);

	virtual uint8_t dsw1();  // read at 0xD004
	virtual uint8_t dsw2();  // read at 0xD005

private:
	void blit_tile_bg(short logical_row);
	void blit_tile_fg(short row, char col);
	void blit_sprite(short row, unsigned char s_idx);
	unsigned short calculate_color_starforce(unsigned char raw_palette_byte);
	void blit_background_line(short start_screen_row, int layer_num);
	unsigned char bg_color_group(unsigned char tile_code, int layer_num);
	void SN76489_Write_3chip(int chip, unsigned char data);
	int sn_last_register[3];

	// Latch per la comunicazione tra CPU principale e CPU audio
	unsigned char sound_latch = 0;
	unsigned char sound_latch_pending = 0; 
	unsigned char sound_irq_toggle = 0;
	
	// Aggiungi queste variabili per i DSW e gli input, se non le hai già
	unsigned short starforce_palette[512];

	unsigned char coinBackup = 0;
	unsigned char coinFrameCounter = 0;

	const Roms roms;

	// Cached: hot path reads these per access; data() checks the cache on every call.
	const unsigned char *rom_main_ptr;
	const unsigned char *rom_sub_ptr;
	const uint32_t (*fg_ptr)[8];
	const uint32_t (*bg_ptr[3])[16][2];  // bg1..bg3
	const uint32_t (*spr16_ptr)[16][2];
	const uint32_t (*spr32_ptr)[32][4];

	// Sprite/scroll/bg state as of the main CPU's vblank IRQ (MAME's render
	// point), handed to the video core via Seqlock (see seqlock.h).
	// prepare_frame()/render read video.*, never live RAM.
	// bg tile RAM is included: the half-rate render draws the bottom half
	// up to two frames later, and live tiles with a snapshot scroll expose
	// the row the game just streamed in at the bottom edge.
	static constexpr uint16_t BG_MAP_SIZE = 16 * 32; // tile cols x rows used
	struct VideoState {
		unsigned char sprite_ram[0x80];
		unsigned char bg_vram[3][BG_MAP_SIZE];  // layers 1..3
		uint16_t bg12_scroll_y; // layers 1+2: hw_control_ram 0x30/0x31
		uint8_t bg12_scroll_x;  // 0x35
		uint16_t bg3_scroll_y;  // layer 3: 0x20/0x21
		uint8_t bg3_scroll_x;   // 0x25
	};
	Seqlock<VideoState> vblank;
	VideoState video = {};
};

#endif