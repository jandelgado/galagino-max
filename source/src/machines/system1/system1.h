#ifndef SYSTEM1_H
#define SYSTEM1_H

#include "../machineBase.h"
#include "../../emulation/seqlock.h"
#include "system1_video.h"
#include "system1_collision.h"
#include "system1_ppi.h"

// Sega System 1 board, 8255 variant (MAME sega/system1.cpp sys1ppi):
//
//   main Z80 4 MHz --PPI A latch, C b7 NMI--> sound Z80 4 MHz --> 2 x SN76489A
//        |
//   VRAM bg page (scrolls) + fg page, 32 line-drawn sprites, palette RAM
//        |                         |
//   render (video core):     collision (emulation core, vblank):
//   sprites -> frame_buffer, sprite vs sprite, sprite vs tiles,
//   lookup PROM mixes layers read back by the game at 0xf000
//
// A game subclass supplies ROMs and DIP switches.
class system1 : public machineBase {
private:
  // ROM assets, unpacked into the Arena by the ctor (declared first: init order)
  Asset<uint32_t[8], COMPRESSED> tile_rom;
  Asset<unsigned char, COMPRESSED> main_rom;
  Asset<unsigned char, COMPRESSED> sprite_rom;
  Asset<unsigned char, COMPRESSED> sound_rom;
  Asset<unsigned char, COMPRESSED> lookup_prom;

public:
  void reset() override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char inZ80(unsigned short Port) override;
  void outZ80(unsigned short Port, unsigned char Value) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;

protected:
  struct Roms {
    const FlashAsset<unsigned char, COMPRESSED> &main_cpu;
    const FlashAsset<unsigned char, COMPRESSED> &sound_cpu;
    const FlashAsset<uint32_t[8], COMPRESSED> &tiles;
    const FlashAsset<unsigned char, COMPRESSED> &sprites;
    const FlashAsset<unsigned char, COMPRESSED> &lookup;
  };
  struct Config {
    system1_video::Visarea vis;
    bool startButton2;  // START presses BUTTON2 (Regulus: bomb)
  };
  system1(const Roms &roms, const Config &config);

  virtual uint8_t dswA() = 0;  // port 0x0c: coinage
  virtual uint8_t dswB() = 0;  // ports 0x0d, 0x10: game options

  bool collided() const { return collision.hit(); }

private:
  static constexpr int MAIN_CPU = 0;
  static constexpr int SOUND_CPU = 1;

  // memory[] layout
  static constexpr uint16_t WORK_RAM = 0x0000;     // c000-cfff
  static constexpr uint16_t SPRITE_RAM = 0x1000;   // d000-d7ff
  static constexpr uint16_t PALETTE_RAM = 0x1800;  // d800-dfff
  static constexpr uint16_t VIDEO_RAM = 0x2000;    // e000-efff
  static constexpr uint16_t SOUND_RAM = 0x3000;    // sound 8000-87ff
  static constexpr uint16_t RAM_END = 0x3800;
  static_assert(RAM_END <= RAMSIZE, "System 1 RAM does not fit memory[]");
  static_assert(VIDEO_RAM == 0xe000 - 0xc000, "c000-efff map 1:1 onto memory[]");

  static constexpr int PALETTE_USED = 0x600;  // sprite, fg, bg blocks of 0x200

  // 260 lines per frame, CPUs interleaved per line (~256 cycles at 4 MHz)
  static constexpr int FRAME_LINES = 260;
  static constexpr int VBLANK_LINE = 224;
  static constexpr int MAIN_STEPS_PER_LINE = 22;   // M1 + VRAM waits: ~3.6 MHz
  static constexpr int SOUND_STEPS_PER_LINE = 11;  // MAME trace: ~2830 per frame
  static constexpr int SOUND_IRQ_FIRST = 32;       // lines 32, 96, 160, 224
  static constexpr int SOUND_IRQ_PERIOD = 64;

  struct VideoState {
    uint8_t sprite_ram[system1_video::SPRITES * system1_video::SPRITE_BYTES];
    uint8_t bg_page[system1_video::PAGE_BYTES];
    uint8_t scroll_x_lo;
    uint8_t scroll_x_hi;
    uint8_t scroll_y;
    uint8_t mode;
  };

  struct ActiveSprite {
    system1_video::Sprite s;
    uint8_t number;
    int16_t x0;     // half-res x of pixel 0
    int16_t width;  // widest row in pixels
  };

  uint8_t mainRead(uint16_t addr);
  void mainWrite(uint16_t addr, uint8_t value);
  uint8_t soundRead(uint16_t addr);
  void soundWrite(uint16_t addr, uint8_t value);
  void irqLines();
  void snWrite(int chip, uint8_t data);
  void snNoise(int chip);
  uint8_t readP1();
  uint8_t readSystem();
  void startVblank();
  void drawSprites(int strip);
  void mixStrip(int strip);

  const uint32_t (*tiles)[8];
  const uint8_t *mainRom;
  const uint8_t *spriteRom;
  const uint8_t *soundRom;
  const uint8_t *prom;
  uint16_t mainRomSize;
  uint16_t soundRomSize;
  uint16_t tileMask;
  uint8_t spriteBanks;
  const Config config;
  int firstStrip;
  int lastStrip;

  Ppi8255 ppi;
  bool mainIrq;              // HOLD_LINE: pending until the CPU enables interrupts
  bool soundIrq;
  uint8_t snLatch[2];        // last latched register per chip
  uint8_t snNoiseReg[2];
  uint16_t palette[PALETTE_USED];
  Collision collision;

  Seqlock<VideoState> vblank;
  VideoState video = {};
  ActiveSprite active[system1_video::SPRITES];
  int activeCount = 0;
};

#endif
