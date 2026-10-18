#ifndef MPATROL_H
#define MPATROL_H

#include "../machineBase.h"
#include "../../emulation/ay8910.h"

#include "../../catalog.h"
#ifdef GAME_MPATROL

#include "mpatrol_logo.h"
#include "mpatrol_dipswitches.h"
#include "mpatrol_video.h"
#include "../../cpus/m6803/m6803.h"
#include "../../emulation/seqlock.h"

// Moon Patrol memory layout in memory[]:
//   0x0000 video RAM  (8000-83ff)
//   0x0400 color RAM  (8400-87ff)
//   0x0800 sprite RAM (c800-cbff, cc00-cfff mirrors)
//   0x0c00 work RAM   (e000-e7ff)
#define MP_MEM_VRAM    0x0000
#define MP_MEM_CRAM    0x0400
#define MP_MEM_SPRITES 0x0800
#define MP_MEM_WORK    0x0c00
#define MP_MEM_END     0x1400
static_assert(MP_MEM_END <= RAMSIZE, "mpatrol RAM does not fit");

class mpatrol : public machineBase
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> mpatrol_rom;
  Asset<unsigned char, COMPRESSED> mpatrol_snd_rom;
  Asset<unsigned short[8], COMPRESSED> mpatrol_tilemap;
  Asset<uint32_t[16], COMPRESSED> mpatrol_spritemap;
  Asset<unsigned char[4096], COMPRESSED> mpatrol_bg;
public:
  mpatrol();

  signed char machineType() override { return MCH_MPATROL; }
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
  // Native ROT0 240x252, shown 1:1 on the portrait panel.
  const int renderWidth()  override { return mpatrol_video::OUT_WIDTH; }
  const int renderBuffer() override { return mpatrol_video::OUT_WIDTH * 2 * 8; }

  unsigned char rdZ80(unsigned short Addr) override;
  void          wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;
  void          outZ80(unsigned short Port, unsigned char Value) override;

  void init(Input *input, unsigned short *framebuffer,
            sprite_S *spritebuffer, unsigned char *memorybuffer) override;
  void reset() override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void) { return mpatrol_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

  // M6803 sound CPU access, called from the C callbacks
  uint8_t snd_read(uint16_t addr);
  void    snd_write(uint16_t addr, uint8_t val);
  void    snd_port_write(uint8_t port, uint8_t val);
  uint8_t snd_port_read(uint8_t port);

private:
  // Render input, latched at vblank (see seqlock.h).
  struct VideoState {
    uint8_t vram[0x400];
    uint8_t cram[0x400];
    uint8_t spr[0x100];
    uint8_t scroll, bg1x, bg1y, bg2x, bg2y, bgctl;
  };
  Seqlock<VideoState> vblank;
  VideoState video;  // render copy, video core only

  void render_line(unsigned short *dst, int line);
  void draw_bg(unsigned short *dst, int line, int img, uint8_t bgx, uint8_t bgy);
  void draw_chars(unsigned short *dst, int line);
  void draw_sprites(unsigned short *dst, int line);

  // Z80 I/O registers
  uint8_t scroll, bg1x, bg1y, bg2x, bg2y, bgctl;
  uint8_t sound_cmd;

  // M6803 sound CPU state (Irem M52 sound board, see motorace)
  m6803_state snd_cpu;
  uint8_t snd_port1;
  uint8_t snd_port2;
  uint8_t ay_addr[2];
  uint8_t ay_regs[2][16];
  int nmi_cycle_cnt;
#ifdef LED_PIN
  uint8_t led_frame = 0;
#endif

  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *rom_ptr = nullptr;
  const unsigned char *snd_rom_ptr = nullptr;
  const unsigned short (*tiles)[8] = nullptr;
  const uint32_t (*sprites)[16] = nullptr;
  const unsigned char (*bgs)[4096] = nullptr;

private:
  static const uint8_t AY_INC = 5;
  static const uint8_t AY_GAIN = 5;
  Ay8910 ay[2] = {Ay8910(AY_INC, AY_GAIN), Ay8910(AY_INC, AY_GAIN)};
};

#endif // GAME_MPATROL
#endif // MPATROL_H
