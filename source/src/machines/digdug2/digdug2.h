#ifndef DIGDUG2_H
#define DIGDUG2_H

// ============================================================================
// Dig Dug II (Namco 1985). Same board as todruaga (MAME mappy.cpp:
// todruaga = digdug2 + gfx_todruaga): 2x MC6809E @ 1.536 MHz (main +
// sound), WSG 15XX 8 voices, tilemap 36x60 with scroll, 64 sprites 16x16
// 4bpp. Differences vs todruaga:
//  - 256 sprite codes (2x 16 KB ROM), 16 sprite color groups
//  - DSW0 bit0 = BUTTON2 (jackhammer), driven by START or B/Y/R1
//  - no watchdog (MAME init_digdug2 nops the write to 0x8000)
// ============================================================================

#include "../machineBase.h"
#include "../../emulation/namco_wsg.h"
#include "../../emulation/seqlock.h"
#include "../namcoio.h"
#include "../../cpus/m6809/m6809.h"
#include "digdug2_rom_main.h"
#include "digdug2_rom_sub.h"
#include "digdug2_tilemap.h"
#include "digdug2_spritemap.h"
#include "digdug2_cmap.h"
#include "digdug2_wavetable.h"
#include "digdug2_dipswitches.h"
#include "digdug2_logo.h"

// CPU interleave as mappy/todruaga (do not raise without DEBUG_TIMING:
// run_frame over 16.6 ms drops frames)
#define DIGDUG2_SLICES 1600

// memory[] layout (RAMSIZE 16 KB), offsets = main CPU addresses up to 0x27FF:
//   0x0000-0x0FFF  VRAM (tiles 0x000-0x7FF, attributes 0x800-0xFFF)
//   0x1000-0x27FF  main work RAM with sprite tables
//     (tab1 code/color 0x1780, tab2 Y/X 0x1F80, tab3 flags 0x2780)
//   0x2800-0x2BFF  shared RAM main 0x4000-0x43FF / sub 0x0000-0x03FF
//     (first 0x40 bytes = WSG 15XX registers -> soundregs[])
#define DIGDUG2_SHARED_OFF 0x2800

class digdug2 : public machineBase
{
protected:
  // Namco custom I/O chip 0 (coins, stick, buttons): 58XX on Dig Dug II,
  // 56XX on Motos (MAME mappy.cpp digdug2() / motos())
  enum IoChip : uint8_t { IO_58XX, IO_56XX };

  // ROMs and tables of a game on this board (motos)
  struct Board {
    const FlashAsset<unsigned short[16], COMPRESSED> &colormapSprites;
    const FlashAsset<unsigned char, COMPRESSED> &romMain;
    const FlashAsset<unsigned char, COMPRESSED> &romSub;
    const FlashAsset<uint32_t[32], COMPRESSED> &sprites;
    const FlashAsset<unsigned short[8], COMPRESSED> &tilemap;
    const unsigned short (*cmapTiles)[4];
    const unsigned short (*cmapPrio)[4];
    const signed char (*wavetable)[32];
    IoChip chip0;
  };
  explicit digdug2(const Board &b);

  // set by io_in() when BUTTON2 is pressed, read by gameLeds()
  unsigned char drill_active = 0;

  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned short[16], COMPRESSED> digdug2_colormap_sprites;
  Asset<unsigned char, COMPRESSED> digdug2_rom_main;
  Asset<unsigned char, COMPRESSED> digdug2_rom_sub;
  Asset<uint32_t[32], COMPRESSED> digdug2_sprites;
  Asset<unsigned short[8], COMPRESSED> digdug2_tilemap;
public:
    digdug2();

    uint8_t machineType() override { return MCH_DIGDUG2; }
    signed char useVideoHalfRate() override { return 1; }

    void reset() override;

    unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
    void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
    unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

    void run_frame(void) override;
    void prepare_frame(void) override;
    void render_row(short row) override;

    void renderAudio(int16_t *out, int n) override {
      wsg.render(soundregs, wavetable, wsg_enable != 0, out, n);
      audioShiftRight(out, n, 1);
    }

    static const Logo &logo(void) { return digdug2_logo; }

#ifdef LED_PIN
    static void menuLeds(CRGB *leds);
    void gameLeds(CRGB *leds) override;
#endif

private:
    static const uint16_t SPRITE_TAB_SIZE = 0x80;

    void blit_tile(unsigned short idx, short x, char prio);
    void blit_sprite(short row, unsigned char s);
    void render_tiles(short row, char prio);
    void mainlatch_w(uint16_t addr);
    void install_rom_direct(void);
    bool poll_idle(m6809_state &cpu, uint16_t loop, uint8_t lda_len, uint16_t flag);

    // Namco custom I/O: chip 0 = 58XX (coins/inputs), chip 1 = 56XX (DIPs)
    void io_chips_reset(void);
    unsigned char io_in(unsigned char chip, unsigned char port);
    bool drill_pressed(unsigned int keys) const;

    m6809_state main_cpu;
    m6809_state sub_cpu;

    const unsigned char *rom_main;               // 32 KB, main 0x8000-0xFFFF
    const unsigned char *rom_sub;                // 8 KB, sub 0xE000-0xFFFF
    const unsigned short (*tiles)[8];            // 4 KB
    const unsigned short (*cmap_tiles)[4];
    const unsigned short (*cmap_prio)[4];
    const unsigned short (*cmap_sprites)[16];    // 16 groups x 16
    const signed char (*wavetable)[32];
    const IoChip chip0;

    NamcoIO io[2];
    unsigned char dipmux_sel;      // LS157: 0 = DSW2 low nibble, 1 = high

    // mainlatch LS259 at 0x5000 main / 0x2000 sub (A0 = data):
    // Q0 sub irq mask, Q1 main irq mask, Q2 flip (ignored),
    // Q3 sound enable, Q4 !reset namcoio, Q5 !reset sub CPU
    unsigned char main_irq_mask, sub_irq_mask;
    unsigned char wsg_enable;
    unsigned char sub_reset, io_reset;

    unsigned char scroll;          // 0x3800-0x3FFF: offset >> 3

    // Sprite tables + scroll latched at vblank, so one screen never mixes
    // two emulated frames. Tile RAM stays live: the game sets scroll once
    // per screen (MAME: only 0 and 32 seen), no smooth scroll.
    struct VideoState {
      unsigned char tab1[SPRITE_TAB_SIZE];   // code, color
      unsigned char tab2[SPRITE_TAB_SIZE];   // Y, X
      unsigned char tab3[SPRITE_TAB_SIZE];   // flags, enable/X msb
      unsigned char scroll;
    };
    Seqlock<VideoState> vblank;
    VideoState video = {};


    Namco15xx wsg;
};

#endif
