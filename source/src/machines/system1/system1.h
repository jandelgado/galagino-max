#ifndef SYSTEM1_H
#define SYSTEM1_H

#include <cstddef>
#include "../machineBase.h"
#include "../../emulation/any_asset.h"
#include "../../emulation/sn76489.h"
#include "../../emulation/seqlock.h"
#include "system1_video.h"
#include "system1_collision.h"
#include "system1_ppi.h"
#include "system1_pio.h"

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
  AnyAsset<uint32_t[8]> tile_rom;
  AnyAsset<unsigned short> tile_map;
  AnyAsset<unsigned char> main_rom;
  AnyAsset<unsigned char> sprite_rom;
  AnyAsset<unsigned char> sound_rom;
  AnyAsset<unsigned char> lookup_prom;
  AnyAsset<unsigned char> opcode_rom;
  AnyAsset<unsigned char> bank_rom;
  AnyAsset<unsigned char> bank_opcode_rom;

public:
  void reset() override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char opPrefixZ80(unsigned short Addr) override { return opZ80(Addr); }
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char inZ80(unsigned short Port) override;
  void outZ80(unsigned short Port, unsigned char Value) override;

  void run_frame(void) override;
  void renderAudio(int16_t *out, int n) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  const int renderWidth() override;
  const int renderBuffer() override { return renderWidth() * 2 * STRIP_LINES; }

protected:
  // Omitted trailing fields in an initializer: absent
  struct Roms {
    AssetRef<unsigned char> main_cpu;
    AssetRef<unsigned char> sound_cpu;
    AssetRef<uint32_t[8]> tiles;
    AssetRef<unsigned short> tileMap;
    AssetRef<unsigned char> sprites;
    AssetRef<unsigned char> lookup;
    // encrypted main CPU: opcode image of 0000-7fff, romconv decrypts
    // main_cpu as data
    AssetRef<unsigned char> opcodes;
    AssetRef<unsigned char> banks;  // 16 KB banks at 8000-bfff (Config::bank)
    const uint16_t *palette;      // palette RAM byte -> RGB565, else system1_palette
    // MC-8123: opcode image of the banks, follows the data bank
    AssetRef<unsigned char> bankOpcodes;
  };
  // sound latch / video mode chip: 8255 PPI (sys1ppi) or Z80 PIO (sys1pio).
  // System 2 is a PPI board with the mute bit inverted: BOARD_SYS2 with
  // row scroll (MAME sys2row), BOARD_SYS2_FIXED with one x scroll (sys2).
  enum Board : uint8_t { BOARD_PPI, BOARD_PIO, BOARD_SYS2, BOARD_SYS2_FIXED };

  // ROT270: panel row = native x (column games). ROT0: panel row = native
  // line, a 240 px window into the 256 px line (landscape games, upright).
  // ROT90: ROT270 turned by 180 degrees.
  enum Orientation : uint8_t { ROT270, ROT0, ROT90 };

  // VRAM: the CPU window only (pages 0-1 in memory[]), or all 8 System 2
  // pages in the Arena, banked into the window by PPI port C bits 1-2.
  // VRAM_PAGED needs ARENA_EXTRA entries of PAGED_VRAM_BYTES and twice
  // PAGED_SNAP_BYTES (bg page copies, published and rendered).
  enum Vram : uint8_t { VRAM_WINDOW, VRAM_PAGED };
  static constexpr uint32_t PAGED_VRAM_BYTES = system1_video::SYS2_PAGES * system1_video::PAGE_BYTES;
  static constexpr uint32_t PAGED_SNAP_BYTES = system1_video::BG_PAGES * system1_video::PAGE_BYTES;

  // main ROM banking at 8000-bfff: none, MAME bank44 (Heavy Metal):
  // bank = video mode bit 6, bit 2, or bank0c (Choplifter): bits 3, 2
  enum Bank : uint8_t { BANK_NONE, BANK_44, BANK_0C };

  // P1 button bits: generic = FIRE on bit 2, START/B on bit 1; swapped for
  // sets whose MAME BUTTON1 sits on bit 1 (Heavy Metal)
  enum Buttons : uint8_t { BUTTONS_GENERIC, BUTTONS_SWAPPED };

  struct Config {
    system1_video::Visarea vis;
    bool startButton2;  // START presses BUTTON2 (Regulus: bomb)
    Board board;        // omitted in an initializer: BOARD_PPI
    Orientation orientation;  // omitted: ROT270
    system1_video::Window window;  // ROT0 only
    Bank bank;          // omitted: BANK_NONE
    Buttons buttons;    // omitted: BUTTONS_GENERIC
    Vram vram;          // omitted: VRAM_WINDOW
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
  static constexpr int STRIP_LINES = 8;

  // 260 lines per frame, CPUs interleaved per line (~256 cycles at 4 MHz)
  static constexpr int FRAME_LINES = 260;
  static constexpr int VBLANK_LINE = 224;
  static constexpr int MAIN_STEPS_PER_LINE = 22;   // M1 + VRAM waits: ~3.6 MHz
  static constexpr int SOUND_STEPS_PER_LINE = 11;  // MAME trace: ~2830 per frame
  static constexpr int SOUND_IRQ_FIRST = 32;       // lines 32, 96, 160, 224
  static constexpr int SOUND_IRQ_PERIOD = 64;

  // bg_page: the window page that is not fg (System 1 page 0, System 2
  // page 1, all Choplifter bg pages); the fg page is read live
  struct VideoState {
    uint8_t sprite_ram[system1_video::SPRITES * system1_video::SPRITE_BYTES];
    uint8_t bg_page[system1_video::PAGE_BYTES];
    system1_video::Layers layers;
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
  uint8_t readP1();
  uint8_t readSystem();
  bool takeSoundNmi();
  uint8_t ackSoundLatch();
  uint8_t videoMode() const;
  bool muted() const;
  system1_video::VideoBoard videoBoard() const;
  void selectBank();
  uint8_t *vramWindow();
  const uint8_t *vramBase() const;
  uint8_t pageMask() const;
  void startVblank();
  void drawSprites(int strip);
  void mixStrip(int strip);
  void renderRot0(int strip);
  int playerX() const;
  system1_video::LineSrc lineSrc() const;

  system1_video::TileSet tiles;
  const uint8_t *mainRom;
  const uint8_t *spriteRom;
  const uint8_t *soundRom;
  const uint8_t *prom;
  const uint8_t *opcodes;  // nullptr: M1 fetches read mainRom
  const uint8_t *banks;    // nullptr: no banked ROM
  const uint8_t *bank;     // current bank at 8000-bfff
  const uint8_t *bankOps;     // nullptr: banks hold their own opcodes
  const uint8_t *bankOpcode;  // current bank's opcodes
  uint8_t *vramPages;     // VRAM_PAGED: 8 pages, else nullptr
  uint8_t *snapPublished; // VRAM_PAGED: bg pages latched at vblank (Seqlock data)
  uint8_t *snapRender;    // VRAM_PAGED: render copy of snapPublished
  const uint16_t *paletteLut;
  uint16_t mainRomSize;
  uint16_t soundRomSize;
  uint8_t spriteBanks;
  const Config config;
  int firstStrip;
  int lastStrip;
  int16_t winX;  // ROT0: native x of panel column 0, 0..16
  system1_video::WindowFollow follow;

  Ppi8255 ppi;
  Z80Pio pio;
  bool mainIrq;              // HOLD_LINE: pending until the CPU enables interrupts
  bool soundIrq;
  uint16_t palette[PALETTE_USED];
  uint16_t mixSel[system1_video::MIX_PRIORITIES];  // lookup PROM per tile priority pair
  bool clearShowsBg;
  Collision collision;

  Seqlock<VideoState> vblank;
  VideoState video = {};
  ActiveSprite active[system1_video::SPRITES];
  int activeCount = 0;
  uint32_t sprTiles[STRIP_LINES];  // per strip line: tile rows with sprite pens

protected:
  Sn76489 sn[2] = {Sn76489(Sn76489::INC_2MHZ), Sn76489(Sn76489::INC_4MHZ)};
};

#endif
