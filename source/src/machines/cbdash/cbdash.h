#ifndef CBDASH_H
#define CBDASH_H

#include "../machineBase.h"
#include "../../cpus/m6502/m6502.h"
#include "../../emulation/seqlock.h"
#include "cbdash_video.h"
#include "cbdash_dipswitches.h"

// Boulder Dash (Data East 1985), DECO Cassette, BIOS B, dongle type 5.
// The tape boot (BIOS + i8041 + 150 s load) is replaced by RAM images cut
// out of the cassette at romconv time.
class cbdash : public machineBase {
  Asset<unsigned char, COMPRESSED> cbdash_rom_main;
  Asset<unsigned char, COMPRESSED> cbdash_rom_sound;
  Asset<unsigned char, COMPRESSED> cbdash_rom_bios;
  Asset<unsigned char, COMPRESSED> cbdash_rom_audio;

public:
  cbdash();

  signed char machineType() override { return MCH_CBDASH; }
  const int renderWidth() override { return 240; }
  const int renderBuffer() override { return 240 * 2 * 8; }
  void start() override;
  void reset() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static constexpr uint32_t MAIN_RAM_SIZE = 0xc000;     // 0000-bfff, incl. charram 6000-bfff
  static constexpr uint16_t SOUND_RAM_OFFSET = 0x0000;  // sound CPU 0000-0fff
  static constexpr uint16_t FG_RAM_OFFSET = 0x1000;     // c000-c3ff
  static constexpr uint16_t COLOR_RAM_OFFSET = 0x1400;  // c400-c7ff
  static constexpr uint16_t TILE_RAM_OFFSET = 0x1800;   // d000-dbff, unused by cbdash
  static constexpr uint16_t MEM_END = TILE_RAM_OFFSET + 0x0c00;
  static_assert(MEM_END <= RAMSIZE, "RAMSIZE too low for cbdash");

  m6502_t cpu_main, cpu_audio;
  uint8_t *main_ram = nullptr;
  const uint8_t *rom_bios = nullptr;
  const uint8_t *rom_audio = nullptr;

  uint8_t palette_ram[32];
  uint8_t ccb = 0;  // e410 color_center_bot: b0 char color, b1 sprite color
  uint8_t sound_latch = 0, sound_latch2 = 0, sound_ack = 0;
  uint8_t ay_addr[2] = {0, 0};
  bool audio_nmi_enable = false;
  bool audio_reset = false;  // e400 b0, sound CPU held while set
  bool vblank = false;
  bool dongle_latch = false;
  bool coin_prev = false;
  uint8_t led_flash = 0;

  // Render input latched at vblank so one screen never mixes two emulated
  // frames: fg codes (incl. sprite list), color banks, palette, ccb, 2 KB.
  // charram (24 KB, the gfx) stays live, like tutankhm's bitmap.
  Seqlock<cbdash_video::VideoState> vblank_state;
  cbdash_video::VideoState video = {};
  uint16_t pens[cbdash_video::PENS];
  cbdash_video::Sprite sprites[cbdash_video::SPRITES];
  uint8_t sprite_count = 0;

  void boot();
  uint8_t in0();
  uint8_t in2();
  static uint8_t main_read(m6502_t *cpu, uint16_t addr);
  static void main_write(m6502_t *cpu, uint16_t addr, uint8_t val);
  static uint8_t main_fetch(m6502_t *cpu, uint16_t addr);
  static uint8_t audio_read(m6502_t *cpu, uint16_t addr);
  static void audio_write(m6502_t *cpu, uint16_t addr, uint8_t val);
};

#endif
