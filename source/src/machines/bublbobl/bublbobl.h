#ifndef BUBLBOBL_H
#define BUBLBOBL_H

#include "bublbobl_dipswitches.h"
#include "bublbobl_video.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/sidplayer.h"
#include "bublbobl_sound.h"

// ============================================================
// Bubble Bobble, bootleg "Bobble Bobble" (MAME boblbobl), no MCU.
// Main Z80 6 MHz, IM 1 IRQ at vblank:
//   0000-7fff ROM, 8000-bfff ROM bank ((data ^ 4) & 7)
//   c000-dcff video RAM, dd00-dfff object RAM (192 x 4)
//   e000-f7ff RAM shared with the sub CPU, f800-f9ff palette RAM
//   fa00 W sound latch, fa01 R semaphores, fa03 W sound reset
//   fa80 W watchdog, fb00 W sub NMI, fb40 W bank/control
//   fc00-fdff RAM, fe00/fe80 IC43 protection, ff00-ff03 DSW0/1 IN0/1
// Sub Z80 6 MHz, IM 1 IRQ at vblank: 0000-7fff ROM, e000-f7ff RAM.
// Sound Z80 + YM2203 + YM3526 are not emulated: latch writes become
// C64 SID music or recorded effect samples.
//
// Unrotated: native lines 16..239 -> panel strips 4..31, native x
// 0..255 -> 224 columns, every 8th pixel dropped (BUBLBOBL_SCALE), or
// 240 columns, 8 pixels dropped at each edge (BUBLBOBL_CROP).
// ============================================================

class bublbobl : public machineBase
{
  // ROM assets; COMPRESSED ones unpacked eagerly in the ctor, largest
  // first, to fit Arena's two blocks
  Asset<unsigned char, COMPRESSED> bublbobl_bank_rom;
  Asset<unsigned char, COMPRESSED> bublbobl_main_rom;
  Asset<unsigned char, COMPRESSED> bublbobl_sub_rom;
  Asset<uint32_t[8], PLAIN> bublbobl_tiles;
  Asset<unsigned short, PLAIN> bublbobl_tile_map;
  Asset<unsigned char, PLAIN> bublbobl_prom;
  Asset<unsigned char, PLAIN> bublbobl_sid;

public:
  bublbobl();

  signed char machineType() override { return MCH_BUBLBOBL; }
  void reset() override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  const int renderWidth() override { return OUT_WIDTH; }
  const int renderBuffer() override { return OUT_WIDTH * 2 * 8; }
  void renderSid(int16_t *out, int n) override;
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
#endif

  static constexpr uint16_t TUNE_RAM_SIZE = 0x10000 - 0xEA40;

private:
  static constexpr uint8_t MAIN_CPU = 0;
  static constexpr uint8_t SUB_CPU = 1;
  static constexpr uint8_t CPUS = 2;

  // 6 MHz each: ~10000 instructions per frame, interleaved 4 : 4
  static constexpr uint16_t SLICES = 2500;

  // idle loops, left only through the vblank IRQ (MAME trace,
  // romconv/internal/bublbobl/trace_idle.lua): stepping them is wasted
  static constexpr uint16_t MAIN_IDLE_PC = 0x01ed; // jr $01ED
  static constexpr uint16_t SUB_IDLE_PC = 0x000a;  // jp $000A

  static constexpr uint16_t BANK_ADDR = 0x8000;
  static constexpr uint16_t BANK_SIZE = 0x4000;
  static constexpr uint8_t BANKS = 4;           // banks 4..7 are empty
  static constexpr uint16_t RAM_BASE = 0xc000;  // memory[0] = $C000
  static constexpr uint16_t SHARE_ADDR = 0xe000;
  static constexpr uint16_t SHARE_END = 0xf800;
  static constexpr uint16_t IO_ADDR = 0xfa00;   // $FA00-$FBFF I/O
  static constexpr uint16_t RAM2_ADDR = 0xfc00; // $FC00-$FDFF RAM
  static constexpr uint16_t IO2_ADDR = 0xfe00;  // $FE00-$FFFF I/O
  static constexpr uint16_t OBJ_OFS = 0x1d00;   // $DD00
  static constexpr uint16_t OBJ_SIZE = 0x300;
  static constexpr uint16_t PAL_OFS = 0x3800;   // $F800
  static constexpr uint16_t PAL_SIZE = 0x200;
  static_assert(0x10000 - RAM_BASE <= RAMSIZE, "bublbobl RAM does not fit memory[]");

  static constexpr uint16_t SOUND_IO_END = 0xfa80;
  static constexpr uint16_t WATCHDOG_END = 0xfb00;
  static constexpr uint16_t SUB_NMI_END = 0xfb40;
  static constexpr uint16_t CTRL_END = 0xfb80;
  static constexpr uint8_t SOUND_LATCH = 0;     // $FA00 (addr & 3)
  static constexpr uint8_t SOUND_SEMAPHORE = 1; // $FA01
  static constexpr uint8_t SOUND_RESET = 3;     // $FA03
  static constexpr uint8_t SEMAPHORE_IDLE = 0xfc;
  static constexpr uint16_t IC43_A_ADDR = 0xfe00;
  static constexpr uint16_t IC43_B_ADDR = 0xfe80;
  static constexpr uint16_t DSW0_ADDR = 0xff00;
  static constexpr uint16_t DSW1_ADDR = 0xff01;
  static constexpr uint16_t IN0_ADDR = 0xff02;
  static constexpr uint16_t IN1_ADDR = 0xff03;

  // $FB40 bank/control
  static constexpr uint8_t CTRL_BANK = 0x07;
  static constexpr uint8_t CTRL_BANK_XOR = 0x04;
  static constexpr uint8_t CTRL_SUB_RUN = 0x10;   // /SBRES
  static constexpr uint8_t CTRL_VIDEO_ON = 0x40;  // /BLACK

  // IN0 ($FF02), active low except coins
  static constexpr uint8_t IN0_LEFT = 0x01;
  static constexpr uint8_t IN0_RIGHT = 0x02;
  static constexpr uint8_t IN0_COIN2 = 0x04;
  static constexpr uint8_t IN0_COIN1 = 0x08;
  static constexpr uint8_t IN0_JUMP = 0x10;
  static constexpr uint8_t IN0_BUBBLE = 0x20;
  static constexpr uint8_t IN0_START1 = 0x40;

  // 256 native columns -> OUT_WIDTH, see config.h
#if defined(BUBLBOBL_SCALE) == defined(BUBLBOBL_CROP)
#error "define exactly one of BUBLBOBL_SCALE, BUBLBOBL_CROP"
#endif
#ifdef BUBLBOBL_CROP
  static constexpr uint16_t OUT_WIDTH = bublbobl_video::CROP_WIDTH;
  static constexpr uint8_t native_x(uint16_t col) { return bublbobl_video::crop_x(col); }
#else
  static constexpr uint16_t OUT_WIDTH = bublbobl_video::SCALE_WIDTH;
  static constexpr uint8_t native_x(uint16_t col) { return bublbobl_video::keep_x(col); }
#endif

  static constexpr uint8_t FIRST_ROW = 4;
  static constexpr uint8_t LAST_ROW = FIRST_ROW + bublbobl_video::STRIPS;
  static constexpr uint8_t PEN_BG = 0xff;
  static constexpr uint8_t PEN_TRANSPARENT = 15;
  static constexpr uint16_t TILE_CODES = 12288;
  static constexpr uint8_t OBJ_TYPES = 8;

  // cached Asset::data(), read per access
  const unsigned char *bank_rom = nullptr;
  const unsigned char *main_rom = nullptr;
  const unsigned char *sub_rom = nullptr;
  const uint32_t (*tiles)[8] = nullptr;
  const unsigned short *tile_map = nullptr;
  const unsigned char *prom = nullptr;      // 8 types x 16 entries
  uint8_t first_new[OBJ_TYPES] = {};

  uint8_t ctrl = 0;          // $FB40
  bool sreset = false;       // $FA03
  bool irq_pending[CPUS] = {};
  uint8_t ic43_a = 0;
  uint8_t ic43_b = 0;
  uint32_t rng = 1;

  // object RAM, palette and control as of the vblank IRQ (seqlock.h);
  // video RAM (object tile columns) stays live
  struct VideoState {
    uint8_t obj[OBJ_SIZE];
    uint8_t pal[PAL_SIZE];
    uint8_t ctrl;
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  // non-empty objects of the current frame, x already chained
  struct Obj {
    bublbobl_video::ObjPos pos;
    uint16_t gfx_offs;
    uint8_t sy;
    uint8_t type;
    uint8_t bank;
  };
  Obj objs[OBJ_SIZE / 4];
  uint8_t obj_count = 0;
  uint16_t palette[256];
  uint8_t pens[8][256];

  unsigned char rdMain(unsigned short Addr);
  void wrMain(unsigned short Addr, unsigned char Value);
  unsigned char rdSub(unsigned short Addr);
  unsigned char rdIo(unsigned short Addr);
  void wrIo(unsigned short Addr, unsigned char Value);
  void writeCtrl(uint8_t value);
  uint8_t in0();
  uint8_t ic43_a_r(uint8_t offset);
  void ic43_a_w(uint8_t offset);
  void ic43_b_w(uint8_t offset, uint8_t data);
  void stepCpu(uint8_t c);
  bool isIdle(uint8_t c, uint16_t idle_pc) const;
  void drawObjectRow(const Obj &o, uint8_t yc, uint8_t line0);
  void onSoundCommand(uint8_t cmd);
  void onSoundReset();

  // sound: latch commands -> SID subtune / effect sample (sound_map.py)
  static constexpr uint8_t STOP_MUSIC = 0xff;
  static constexpr uint8_t NO_EFFECT = 0xff;
  static constexpr int32_t FX_GAIN = 3;    // +-127 -> about +-380, SID is +-480
  static constexpr int32_t SAMPLE_MAX = 511;

  SidPlayer player;
  PsidInfo tune = {};
  bool tune_ok = false;
  bool sid_on = false;                     // audio core
  volatile uint16_t music_request = 0;     // sequence << 8 | subtune, 0 = stop
  uint16_t music_seen = 0;                 // audio core
  uint8_t music_subtune = 0;               // emulation core
  std::atomic<uint8_t> reset_request{0};   // sound resets, emulation core
  uint8_t reset_seen = 0;                  // audio core
  bublbobl_sound::FxRing fx_ring;
  bublbobl_sound::FxMixer fx;
};

#endif
