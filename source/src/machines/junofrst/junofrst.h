#ifndef JUNOFRST_H
#define JUNOFRST_H

#include "../machineBase.h"
#include "../../emulation/ay8910.h"
#include "../../emulation/i8039_dac.h"
#include "../../emulation/seqlock.h"
#include "../../cpus/m6809/m6809.h"
#include "junofrst_dipswitches.h"
#include "junofrst_logo.h"

// Juno First (Konami 1983), MAME konami/junofrst.cpp, set junofrst.
// Tutankham video board plus a blitter, Mega Zone sound board:
//
//   main KONAMI-1 1.5 MHz --latch 8050, IRQ 8040--> sound Z80 1.79 MHz
//     32 KB 4bpp bitmap, 16 pens                      AY-3-8910
//     blitter: 16x16 nibbles from blitter ROM         |  latch2 5000, INT 6000
//                                                     v
//                                          i8039 8 MHz -> P1 8-bit DAC
//
// Video ROT90: native 256 x 256, visible Y 16..239 -> panel 224 x 256 in
// rows 2..33 (as tutankhm, no scroll register).

// memory[] offsets
#define JUNOFRST_WORKRAM   0x0000   // 8100-8fff
#define JUNOFRST_PALETTE   0x0f00   // 8000-800f
#define JUNOFRST_RAM_END   0x0f10
static_assert(JUNOFRST_RAM_END <= RAMSIZE, "junofrst RAM exceeds memory[]");

class junofrst : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> junofrst_rom;
  Asset<unsigned char, COMPRESSED> junofrst_bank;
  Asset<unsigned char, COMPRESSED> junofrst_snd;
  Asset<unsigned char, COMPRESSED> junofrst_i8039;
  Asset<unsigned char, COMPRESSED> junofrst_blit;
  Asset<unsigned short, COMPRESSED> junofrst_pens;   // RGB565 per palette byte

public:
  junofrst();
  uint8_t machineType() override { return MCH_JUNOFRST; }
  void renderAudio(int16_t *out, int n) override;
  void reset() override;
  void start() override;

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  void wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char val) override;
  unsigned char rdI8048_port(struct i8048_state_S *state, unsigned char port) override;
  unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) override;
  unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif
  static const Logo &logo(void) { return junofrst_logo; }

private:
  static const uint32_t VRAM_SIZE = 0x8000;
  static const uint8_t BANKS = 12;   // 4 KB each; selects 12-15 read open bus
  static const uint16_t BLIT_SIZE = 0x6000;
  static const int32_t I8039_HZ = 8000000 / 15;   // i8039 machine cycles per second

  // Render input latched at vblank (seqlock.h): the palette. The bitmap
  // is drawn by the CPU and the blitter and stays live (as tutankhm).
  struct VideoState {
    uint8_t palette[16];
  };

  uint8_t readInputs(uint16_t addr);
  void latchWrite(uint8_t bit, uint8_t state);
  void blit(void);
  uint8_t ayPortA(void);

  m6809_state main_cpu;
  I8039Dac dac{I8039_HZ, 2};       // (P1 - 128) * 2 next to the AY
  Seqlock<VideoState> vblank;
  VideoState video = {};
  uint16_t pens[16];               // RGB565 of video.palette

  uint8_t *videoram = nullptr;     // Arena, allocated in start()
  uint8_t irq_enable = 0;
  uint8_t irq_toggle = 0;          // vblank IRQ every 2nd frame (30 Hz)
  uint8_t bank = 0;
  uint8_t blitter[4] = {};         // 8070-8073: dest hi/lo, src hi/lo
  uint8_t sound_latch = 0, sound_latch2 = 0;
  uint8_t snd_irq_last = 0;        // 8040 bit 0, IRQ on the rising edge
  uint8_t snd_irq = 0;             // pending until the Z80 enables IRQs
  uint8_t ay_addr = 0;
  uint8_t i8039_status = 0;        // P2 b4-6 -> AY port A b0-2
  uint32_t snd_cycles = 0;         // Z80 cycles, for the AY port A timer
  uint8_t snd_ram[0x400];

  const uint8_t *main_rom = nullptr, *bank_rom = nullptr, *sound_rom = nullptr;
  const uint8_t *i8039_rom = nullptr, *blit_rom = nullptr;
  const uint16_t *pen_rgb = nullptr;

  static const uint8_t AY_INC = 9;   // 1.789772 MHz / 8 / 24 kHz
  static const uint8_t AY_GAIN = 6;
  Ay8910 ay[1] = {Ay8910(AY_INC, AY_GAIN)};
};

#endif
