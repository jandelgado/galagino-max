#ifndef BOMBJACK_H
#define BOMBJACK_H

#include "bombjack_arena_plan.h"
#include "bombjack_dipswitches.h"
#include "bombjack_logo.h"
#include "../tileaddr.h"
#include "../machineBase.h"
#include "../../emulation/ay8910.h"

#define PORT_P1_IN      0xB000  // P1 Controls (JOYSTICK + BUTTON)
#define PORT_P2_IN      0xB001  // P2 Controls (per modalità COCKTAIL)
#define PORT_SYSTEM_IN  0xB002  // System Inputs (COIN + START)
#define PORT_WATCHDOG   0xB003  // Watchdog Reset (lettura resetta il timer)
#define PORT_DSW1_IN    0xB004  // DIP Switch 1
#define PORT_DSW2_IN    0xB005  // DIP Switch 2

class bombjack : public machineBase
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  bombjack_bg_maps_asset bombjack_bg_maps;
  bombjack_bg_tiles_asset bombjack_bg_tiles;
  bombjack_fg_tiles_asset bombjack_fg_tiles;
  bombjack_rom_cpu1_asset bombjack_rom_cpu1;
  bombjack_rom_cpu2_asset bombjack_rom_cpu2;
  bombjack_sprites_16x16_asset bombjack_sprites_16x16;
  bombjack_sprites_32x32_asset bombjack_sprites_32x32;
public:
  bombjack();

  void reset() override;
  uint8_t machineType() override { return MCH_BOMBJACK; }
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
  signed char useVideoHalfRate() override { return 1; }

  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  void outZ80(unsigned short Port, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char inZ80(unsigned short Port) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
    static const Logo &logo(void);

#ifdef LED_PIN	
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

protected:
  void blit_tile_bg(short logical_row);
  void blit_tile_fg(short row, char col);
  void blit_sprite(short row, unsigned char s_idx);

private:
  uint16_t bombjack_palette[128];
  uint8_t palette[256];

  unsigned char sound_latch; // Scritta dalla Main CPU, letta dalla Audio CPU
  unsigned char sound_latch_backup = 0;
  unsigned char ay_address[3];

  unsigned char m_bg_image;  // Contiene il codice dello sfondo e il bit di visibilità
  bool m_nmi_on;              // Flag per abilitare l'interrupt NMI
  bool m_mmi_skip_audio_cpu;
  bool m_flip;                // Flag per lo screen flip

  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *rom_cpu1_ptr = nullptr;
  const unsigned char *rom_cpu2_ptr = nullptr;
  const unsigned char *bg_maps_ptr = nullptr;

private:
  // 8 KB audio ROM chip; the rest of its 16 KB region reads 0xFF.
  static const uint16_t AUDIO_ROM_SIZE = 0x2000;
  static const uint8_t AY_INC = 8;
  static const uint8_t AY_GAIN = 4;
  Ay8910 ay[3] = {Ay8910(AY_INC, AY_GAIN), Ay8910(AY_INC, AY_GAIN), Ay8910(AY_INC, AY_GAIN)};
};

#endif
