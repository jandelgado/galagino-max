#ifndef TP84_H
#define TP84_H

#include "../machineBase.h"
#include "../../emulation/sn76489.h"
#include "../../cpus/m6809/m6809.h"
#include "../../emulation/seqlock.h"
#include "tp84_hw.h"
#include "tp84_dipswitches.h"
#include "tp84_logo.h"

// Time Pilot '84 (Konami 1984), MAME konami/tp84.cpp, set tp84.
// main 6809 + sub 6809 @ 1.536 MHz, sound Z80 @ 3.58 MHz, 3x SN76489 @ 1.79 MHz.
// Video ROT90: native (X, Y) -> pixel row X + 16, column 239 - Y.

// memory[] offsets
#define TP84_BG_VRAM  0x0000   // main 0x4000
#define TP84_FG_VRAM  0x0400   // main 0x4400
#define TP84_BG_CRAM  0x0800   // main 0x4800
#define TP84_FG_CRAM  0x0C00   // main 0x4C00
#define TP84_SHARED   0x1000   // main 0x5000, sub 0x8000 (2 KB)
#define TP84_SUB_RAM  0x1800   // sub 0x6000-0x679F
#define TP84_SND_RAM  0x2000   // Z80 0x4000-0x43FF
#define TP84_RAM_END  0x2400
static_assert(TP84_RAM_END <= RAMSIZE, "tp84 RAM exceeds memory[]");

#define TP84_LINES           256
#define TP84_CYCLES_LINE     100   // 1.536 MHz / 60 / 256
#define TP84_Z80_STEPS_LINE  20    // circusc calibration: ~5000 Z80 instr/frame
#define TP84_Z80_CYCLES_LINE 233   // 3.58 MHz / 60 / 256, for the 0x8000 timer

// Poll loop skip: polls with no write by that CPU in between = spinning
#define TP84_MAIN_SPIN     3       // main reads of 0x523A (task loop 83AF)
#define TP84_SUB_SPIN      4       // sub reads of 0x2000 (beam loop F104)
#define TP84_SUB_SPIN_END  0xE0    // beam loop exits once line + 0x20 carries
#define TP84_SND_SPIN      2       // Z80 reads of 0x8000 in one line (01F4/01FD)
#define TP84_SND_LOOP      4       // instructions per Z80 timer loop

#define TP84_SND_PLAYER_DIE  0x13  // soundlatch: player ship explodes (MAME probe)
#define TP84_LED_FLASH_FRAMES 30

class tp84 : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> tp84_rom_main;
  Asset<unsigned char, COMPRESSED> tp84_rom_sub;
  Asset<unsigned char, COMPRESSED> tp84_rom_audio;
  Asset<uint16_t[8], COMPRESSED> tp84_tiles;
  Asset<unsigned char[16][8], COMPRESSED> tp84_sprites;
  Asset<uint16_t, COMPRESSED> tp84_rgb;
  Asset<unsigned char, COMPRESSED> tp84_lut;

public:
  tp84();
  signed char machineType() override { return MCH_TP84; }
  void renderAudio(int16_t *out, int n) override { Sn76489::renderBank(sn, out, n); }
  void reset() override;
  void start() override;

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
  static const Logo &logo(void) { return tp84_logo; }

private:
  struct VideoState {
    uint8_t bg_vram[0x400], bg_cram[0x400];
    uint8_t hud_vram[32][4], hud_cram[32][4]; // fg tile columns 0,1,30,31 per tile row
    uint8_t scroll_x, scroll_y, palette_bank;
    uint8_t n_sprites;
    tp84_hw::Sprite sprites[tp84_hw::MAX_SPRITES];
  };

  uint8_t main_read_io(uint16_t addr);
  void sn_write(int chip, uint8_t data);
  void publish_video();
  void build_cmaps(uint8_t bank);
  void blit_sprite(short row, const tp84_hw::Sprite &s);

  m6809_state main_cpu, sub_cpu;
  tp84_hw::SpriteBands bands;
  Seqlock<VideoState> vblank;
  VideoState video;

  uint8_t line = 0;               // current beam line, read by the sub CPU at 0x2000
  uint8_t main_irq_enable = 0, sub_irq_mask = 0;  // not irq_enable: machineBase member
  uint8_t palette_bank = 0, scroll_x = 0, scroll_y = 0;
  uint8_t soundlatch = 0, snd_irq_pend = 0;
  uint8_t sn_last_reg[3] = {0, 0, 0};
  uint16_t snd_cycles = 0;        // Z80 cycle counter mod 16384 for the 0x8000 timer
  uint8_t main_polls = 0, sub_polls = 0, snd_polls = 0;  // polls since the CPU's last write
  volatile uint8_t led_flash = 0; // set by the emulation core

  const uint8_t *main_rom = nullptr, *sub_rom = nullptr, *audio_rom = nullptr;
  const uint16_t (*tiles)[8] = nullptr;
  const uint8_t (*sprite_gfx)[16][8] = nullptr;
  const uint16_t *rgb = nullptr;
  const uint8_t *lut = nullptr;

  uint16_t cmap_tiles[16][4];     // current bank, char colors
  uint16_t cmap_sprites[16][16];  // current bank, sprite colors
  uint16_t sprite_opaque[16];     // bit p set = pen p opaque
  int16_t cmap_bank = -1;         // bank cmap_* were built for

protected:
  Sn76489 sn[3] = {Sn76489(Sn76489::INC_2MHZ), Sn76489(Sn76489::INC_2MHZ), Sn76489(Sn76489::INC_2MHZ)};
};

#endif
