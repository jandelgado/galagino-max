#ifndef GBERET_H
#define GBERET_H

#include "../machineBase.h"
#include "../../emulation/sn76489.h"

#ifdef ENABLE_GBERET

#include "gberet_logo.h"
#include "gberet_dipswitches.h"
#include "gberet_hw.h"
#include "../../emulation/seqlock.h"

// memory[] = CPU address - 0xc000 for c000-dfff, scroll RAM appended
#define GB_MEM_CRAM   0x0000  // c000-c7ff color RAM
#define GB_MEM_VRAM   0x0800  // c800-cfff video RAM
#define GB_MEM_SPR2   0x1000  // d000-d0ff sprite bank, shown when e043 b3 = 1
#define GB_MEM_SPR    0x1100  // d100-d1ff sprite bank, shown when e043 b3 = 0
#define GB_MEM_SCROLL 0x2000  // e000-e03f row scroll low byte / bit 8
#define GB_MEM_END    0x2040
static_assert(GB_MEM_END <= RAMSIZE, "gberet RAM does not fit");

class gberet : public machineBase
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> gberet_rom;
  Asset<uint32_t[8], COMPRESSED> gberet_tilemap;
  Asset<uint32_t[16][2], COMPRESSED> gberet_spritemap;
public:
  gberet();

  signed char machineType() override { return MCH_GBERET; }
  void renderAudio(int16_t *out, int n) override { Sn76489::renderBank(sn, out, n); }
  // Native ROT0 240x224, shown 1:1 on the portrait panel.
  const int renderWidth()  override { return gberet_hw::OUT_WIDTH; }
  const int renderBuffer() override { return gberet_hw::OUT_WIDTH * 2 * 8; }

  unsigned char rdZ80(unsigned short Addr) override;
  void          wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;

  void reset() override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void) { return gberet_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  // Render input, latched at vblank (see seqlock.h). Visible tile rows only.
  struct VideoState {
    uint8_t cram[gberet_hw::VIS_TILES];
    uint8_t vram[gberet_hw::VIS_TILES];
    uint8_t spr[gberet_hw::SPRITE_BYTES];  // bank selected at vblank
    uint16_t scroll[gberet_hw::VIS_TILE_ROWS];
  };
  Seqlock<VideoState> vblank;
  VideoState video;  // render copy, video core only

  void interrupt_tick();
  void sn_write(uint8_t data);
  void render_line(unsigned short *dst, int line);

  uint8_t int_mask = 0;     // e044 b0 NMI, b1/b2 IRQ enable
  uint8_t int_ticks = 0;
  bool nmi_line = false;    // held until e044 clears b0
  bool irq_line = false;    // held until e044 clears b1/b2
  uint8_t sprite_bank = 0;  // e043
  uint8_t sn_latch = 0;     // f200
  uint8_t sn_reg = 0;       // SN76489 latched register
#ifdef LED_PIN
  uint8_t led_frame = 0;
#endif

  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *rom_ptr = nullptr;
  const uint32_t (*tiles)[8] = nullptr;
  const uint32_t (*sprites)[16][2] = nullptr;

protected:
  Sn76489 sn[1] = {Sn76489(Sn76489::INC_1536KHZ)};
};

#endif // ENABLE_GBERET
#endif // GBERET_H
