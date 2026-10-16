#ifndef MIKIE_H
#define MIKIE_H

#include "../machineBase.h"
#include "../../emulation/sn76489.h"
#include "../../cpus/m6809/m6809.h"
#include "../../emulation/seqlock.h"
#include "mikie_dipswitches.h"
#include "mikie_logo.h"

// Mikie (Konami 1984), MAME konami/mikie.cpp, set mikie.
// main 6809 @ 1.536 MHz, sound Z80 @ 3.58 MHz, SN76489A @ 1.79 + 3.58 MHz.
// Video ROT270: native (X, Y), visible Y 16..239 -> display line 255 - X,
// column Y - 16.

// memory[] offsets
#define MIKIE_ZP       0x0000   // main 0x0000-0x00FF
#define MIKIE_RAM      0x0100   // main 0x2800-0x37FF (sprites at 0x2800)
#define MIKIE_CRAM     0x1100   // main 0x3800-0x3BFF
#define MIKIE_VRAM     0x1500   // main 0x3C00-0x3FFF
#define MIKIE_SND_RAM  0x1900   // Z80 0x4000-0x43FF
#define MIKIE_RAM_END  0x1D00
static_assert(MIKIE_RAM_END <= RAMSIZE, "mikie RAM exceeds memory[]");

#define MIKIE_SPRITE_BYTES   0x90
#define MIKIE_LINES          256
#define MIKIE_CYCLES_LINE    100   // 1.536 MHz / 60 / 256
#define MIKIE_Z80_STEPS_LINE 20    // tp84 calibration, same Z80 clock
#define MIKIE_Z80_CYCLES_LINE 233  // 3.58 MHz / 60 / 256, for the 0x8005 timer

class mikie : public machineBase {
protected:
  Asset<unsigned char, COMPRESSED> mikie_rom_main;
  Asset<unsigned char, COMPRESSED> mikie_rom_audio;
  Asset<uint32_t[8], COMPRESSED> mikie_tiles;
  Asset<unsigned char[16][8], COMPRESSED> mikie_sprites;
  Asset<uint16_t, COMPRESSED> mikie_rgb;
  Asset<unsigned char, COMPRESSED> mikie_lut;

public:
  mikie();
  signed char machineType() override { return MCH_MIKIE; }
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
  static const Logo &logo(void) { return mikie_logo; }

private:
  struct VideoState {
    uint8_t vram[0x400], cram[0x400];
    uint8_t sprites[MIKIE_SPRITE_BYTES];
    uint8_t palette_bank;
  };

  uint8_t read_io(uint16_t addr);
  void write_latch(uint8_t bit, uint8_t val);
  void publish_video();
  void build_cmaps(uint8_t bank);
  void blit_sprite(short row, const uint8_t *s, uint32_t front);

  m6809_state main_cpu;
  Seqlock<VideoState> vblank;
  VideoState video;

  uint8_t irq_mask = 0, sound_on = 0;   // LS259 Q7, Q2
  uint8_t palette_bank = 0;
  uint8_t soundlatch = 0, snd_irq_pend = 0;
  uint32_t snd_cycles = 0;              // Z80 cycles mod 2^17 for the 0x8005 timer

  const uint8_t *main_rom = nullptr, *audio_rom = nullptr;
  const uint32_t (*tiles)[8] = nullptr;
  const uint8_t (*sprite_gfx)[16][8] = nullptr;
  const uint16_t *rgb = nullptr;
  const uint8_t *lut = nullptr;

  uint16_t cmap_tiles[16][16];    // current bank, char colors
  uint16_t cmap_sprites[16][16];  // current bank, sprite colors
  int16_t cmap_bank = -1;         // bank cmap_* were built for

protected:
  Sn76489 sn[2] = {Sn76489(Sn76489::INC_1789KHZ, Sn76489::HOLD_SHORT_NOTES),
                   Sn76489(2 * Sn76489::INC_1789KHZ, Sn76489::HOLD_SHORT_NOTES)};
};

#endif
