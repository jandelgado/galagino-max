#ifndef SCREGG_H
#define SCREGG_H

#include "scregg_arena_plan.h"
#include "scregg_logo.h"
#include "scregg_dipswitches.h"
#include "../../cpus/m6502/m6502.h"
#include "../machineBase.h"
#include "../../emulation/ay8910.h"

class scregg : public machineBase {
protected:
  // ROM assets, unpacked into the Arena on first data()
  scregg_chartiles_asset scregg_chartiles;
  scregg_rom_asset scregg_rom;
  scregg_spritetiles_asset scregg_spritetiles;
public:
  scregg();

  uint8_t machineType() override { return MCH_SCREGG; }
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
  void start() override;
  void reset() override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  static const Logo &logo() { return scregg_logo; }

  const int   renderWidth() { return 240; }
  const int   renderBuffer() { return 240 * 2 * 8; }

private:
  static constexpr unsigned short WORK_RAM_OFFSET = 0x0000;
  static constexpr unsigned short VIDEO_RAM_OFFSET = 0x0800;
  static constexpr unsigned short COLOR_RAM_OFFSET = 0x0c00;
  static constexpr unsigned short MEM_FREE = 0x1000;
  static_assert(MEM_FREE <= RAMSIZE, "RAMSIZE too low for scregg");

  static uint8_t main_read(m6502_t *cpu, uint16_t addr);
  static void main_write(m6502_t *cpu, uint16_t addr, uint8_t value);
  static unsigned short xy_swap(unsigned short offset);
  void build_palette();
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char index) override;

  m6502_t m_cpu;
  unsigned char *work_ram = nullptr;
  unsigned char *video_ram = nullptr;
  unsigned char *color_ram = nullptr;
  unsigned short palette[8] = {};
  unsigned char ay_address[2] = {};
  unsigned char flip_screen = 0;
  unsigned char vblank = 0;
  bool coin_latched = false;
  unsigned long coin_pulse_until = 0;
  struct scregg_sprite_s {
    short x, y;
    unsigned char code;
    unsigned char flip_x, flip_y;
  } sprite_list[8];
  unsigned char sprite_count = 0;

  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *rom_ptr = nullptr;

private:
  static const uint8_t AY_INC = 8;
  static const uint8_t AY_GAIN = 7;
  Ay8910 ay[2] = {Ay8910(AY_INC, AY_GAIN), Ay8910(AY_INC, AY_GAIN)};
};

#endif
