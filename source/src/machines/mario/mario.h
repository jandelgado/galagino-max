#ifndef MARIO_H
#define MARIO_H

#include "mario_dipswitches.h"
#include "mario_video.h"
#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "../../emulation/ima_adpcm.h"

// ============================================================
// Mario Bros. (Nintendo 1983, MAME mario, US rev G)
//
// Main Z80 4 MHz: 0000-5fff ROM, 6000-6fff RAM, 7000-73ff sprite RAM,
// 7400-77ff tile RAM (32x32), f000-ffff ROM. 7c00 IN0, 7c80 IN1,
// 7f80 DSW. Writes: 7d00 scroll y, 7e00 tune, 7e80-7e87 LS259 latch,
// 7c00/7c80/7f00-7f07 sound. IO port 00 Z80 DMA: copies the sprite
// list 6900 -> 7000 (0x180 bytes) when latch bit 5 rises. NMI at vblank.
//
// Sound: M58715 (8035) 11 MHz / 15 into an 8 bit DAC (MOVX writes), run
// on the emulation core, DAC sampled at 24 kHz into a ring the audio
// core drains. Walk and skid are discrete circuits on the board,
// synthesized by romconv and played as samples.
//
//   emulation core                          audio core
//   Z80 slice, 8035 slice --dac--> ring --> renderAudio(): DAC + voices
//
// Unrotated 256x224 (native lines 16..239 -> panel strips 4..31). The
// board runs with the flip latch set: the cabinet monitor is mounted
// upside down. 256 columns -> 240, see mario_video.h and config.h.
// ============================================================

class mario : public machineBase
{
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> mario_rom_main;
  Asset<unsigned short[8], COMPRESSED> mario_tiles;
  Asset<uint32_t[32], COMPRESSED> mario_sprites;
  Asset<unsigned short, COMPRESSED> mario_palette;
  Asset<unsigned char, COMPRESSED> mario_rom_sound;
  Asset<unsigned char, COMPRESSED> mario_samples[3];  // walk0, walk1, skid

public:
  mario();

  signed char machineType() override { return MCH_MARIO; }
  void reset() override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  void wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char pos) override;
  unsigned char rdI8048_port(struct i8048_state_S *state, unsigned char port) override;
  unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) override;
  void wrI8048_xdm(struct i8048_state_S *state, unsigned char addr, unsigned char data) override;
  unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) override;
  void renderAudio(int16_t *out, int n) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  const int renderWidth() override { return mario_video::OUT_WIDTH; }
  const int renderBuffer() override { return mario_video::OUT_WIDTH * 2 * 8; }
  static const Logo &logo(void);

#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
#if defined(MARIO_SCROLL) + defined(MARIO_SCALE) + defined(MARIO_CROP) != 1
#error "define exactly one of MARIO_SCROLL, MARIO_SCALE, MARIO_CROP"
#endif

  // 4 MHz Z80 per 16 ms frame (6 x AUDIO_CHUNK samples at 24 kHz)
  static constexpr int32_t Z80_CYCLES_FRAME = 64000;
  static constexpr uint8_t SLICES = 16;
  static constexpr uint16_t SAMPLES_FRAME = 6 * 64;  // AUDIO_CHUNK
  // 8035: 11 MHz / 15 cycles per second over 24000 samples, Q16
  static constexpr uint32_t SOUND_CYCLES_SAMPLE_Q16 = (uint32_t)(733333ull * 65536 / 24000);
  static constexpr uint16_t DAC_RING = 1024;  // power of 2

  // CPU addresses, memory[] holds 0x6000-0x77ff
  static constexpr uint16_t RAM_BASE = 0x6000;
  static constexpr uint16_t RAM_END = 0x7800;
  static constexpr uint16_t ROM_HIGH = 0xf000;    // stored at ROM_HIGH_OFS
  static constexpr uint16_t ROM_HIGH_OFS = 0x6000;
  static constexpr uint16_t IN0_ADDR = 0x7c00;
  static constexpr uint16_t IN1_ADDR = 0x7c80;
  static constexpr uint16_t SCROLL_ADDR = 0x7d00;
  static constexpr uint16_t TUNE_ADDR = 0x7e00;
  static constexpr uint16_t LATCH_ADDR = 0x7e80;  // 8 bits, data bit 0
  static constexpr uint16_t SOUND_ADDR = 0x7f00;  // 8 bits, data bit 0
  static constexpr uint16_t DSW_ADDR = 0x7f80;
  static constexpr uint8_t SCROLL_REST = 0xef;  // 7d00 when no POW bump
  static constexpr uint16_t WALK_MARIO_ADDR = 0x7c00;  // write: SOUND0
  static constexpr uint16_t WALK_LUIGI_ADDR = 0x7c80;  // write: SOUND1

  // memory[] offsets
  static constexpr uint16_t DMA_SRC = 0x0900;     // 0x6900
  static constexpr uint16_t SPRITE_RAM = 0x1000;  // 0x7000
  static constexpr uint16_t TILE_RAM = 0x1400;    // 0x7400
  static constexpr uint16_t SPRITE_BYTES = 0x180; // DMA length
  static constexpr uint16_t TILE_BYTES = 0x400;
  static constexpr uint16_t PLAYER1_X = 0x0107;   // 0x6107, Mario's sprite x
  // main loop waits for the NMI to set the frame flag (ROM 01cb):
  //   01cb LD A,(6010) / AND A / JR NZ,018f / JR 01cb
  static constexpr uint16_t IDLE_PC = 0x01cb;
  static constexpr uint16_t IDLE_FLAG = 0x0010;   // 0x6010
  static_assert(RAM_END - RAM_BASE <= RAMSIZE, "RAM does not fit memory[]");

  // LS259 latch bits (7e80 + n)
  enum : uint8_t {
    LATCH_GFX_BANK = 0,
    LATCH_FLIP = 2,
    LATCH_PAL_BANK = 3,
    LATCH_NMI = 4,
    LATCH_DMA = 5,
  };

  // 7f00 + n sound writes, data bit 0
  enum : uint8_t {
    SOUND_IRQ = 0,      // 8035 INT line (death)
    SOUND_T0 = 1,
    SOUND_T1 = 2,
    SOUND_P1_BIT0 = 3,  // 7f03-7f06 -> P1 bits 0-3
    SOUND_SKID = 7,
  };
  // walk sound burst: the game writes 3, 2, 1 on 3 frames per step
  static constexpr uint8_t WALK_START = 3;

  // 8035 ports: P2 bit 5 clear maps ROM at 000-7ff, bit 7 selects the
  // tune latch for MOVX reads, bits 0-2 the ROM page otherwise
  static constexpr uint8_t P2_INTERNAL = 0x20;
  static constexpr uint8_t P2_TUNE = 0x80;
  static constexpr uint8_t P2_GROUNDED = 0x10;  // reads 0
  static constexpr uint16_t SOUND_BANK = 0x800;

  // IN0 / IN1, active high
  static constexpr uint8_t IN0_RIGHT = 0x01;
  static constexpr uint8_t IN0_LEFT = 0x02;
  static constexpr uint8_t IN0_JUMP = 0x10;
  static constexpr uint8_t IN0_START1 = 0x20;
  static constexpr uint8_t IN1_COIN1 = 0x40;

  // panel rows of the 224 visible native lines
  static constexpr uint8_t VIS_TOP = 16;
  static constexpr uint8_t FIRST_ROW = 4;
  static constexpr uint8_t LAST_ROW = FIRST_ROW + 224 / 8;
  static constexpr uint8_t SPRITES = SPRITE_BYTES / 4;

  void writeLatch(uint8_t bit, uint8_t value);
  void writeSound(uint8_t bit, uint8_t value);
  void runSound(uint16_t samples);
  void playSample(uint8_t i);
  void runZ80(int32_t cycles);
  uint8_t in0();
  void drawTiles(uint8_t line, uint8_t *pens);
  void drawSprites(uint8_t line, uint8_t *pens);
  static uint8_t nativeX(uint16_t col, uint8_t pan);

  const unsigned char *rom = nullptr;
  const unsigned short (*tiles)[8] = nullptr;
  const uint32_t (*sprite_gfx)[32] = nullptr;
  const unsigned short *palette = nullptr;
  const unsigned char *sound_rom = nullptr;

  i8048_state_S snd;
  uint8_t p1 = 0;     // 8035 P1 latch, also written by 7f03-7f06
  uint8_t p2 = 0xff;  // 8035 P2 latch
  uint8_t dac = 0;
  uint8_t skid = 0;   // 7f07 bit 0 as written
  uint32_t sound_due = 0;   // 8035 cycle of the next DAC sample
  uint32_t sound_frac = 0;  // Q16 fraction of sound_due

  // DAC samples, producer run_frame(), consumer renderAudio()
  volatile uint8_t dac_ring[DAC_RING];
  volatile uint16_t dac_wr = 0;
  volatile uint16_t dac_rd = 0;
  uint8_t dac_last = 0;
  int32_t dc_in = 0, dc_out = 0;  // DC blocker state, Q8
  ima_adpcm::Voice voices[3];

  uint8_t latch = 0;   // LS259 outputs, bit n = 7e80 + n
  uint8_t scroll = 0;  // 7d00 raw value
  uint8_t tune = 0;    // 7e00 sound latch

  // tile RAM, sprite list and video registers as of the vblank NMI
  // (seqlock.h); render reads only `video`
  struct VideoState {
    uint8_t tiles[TILE_BYTES];
    uint8_t sprites[SPRITE_BYTES];
    uint8_t scroll;
    uint8_t latch;
    uint8_t player1_x;
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  // sprites visible this frame, in screen coordinates
  struct Sprite {
    int16_t x, y;
    uint8_t code;
    uint8_t pen;   // color * 8
    uint8_t flip;  // SpriteFlip bits
  };
  Sprite sprite_list[SPRITES];
  uint8_t sprite_count = 0;
  uint8_t pan = 0;

  uint8_t pens[8][256];
};

#endif
