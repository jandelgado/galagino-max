#ifndef SYSTEM1_H
#define SYSTEM1_H

#include <cstddef>
#include "../machineBase.h"
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
protected:
  // One ROM asset as romconv emitted it: an LZMA blob (unpacked into the
  // Arena) or a PLAIN flash array (too big for the Arena). Converts from
  // either blob, so a game's Roms initializer lists blobs as they come.
  template <typename T> struct RomRef {
    const FlashAsset<T, COMPRESSED> *packed;
    const FlashAsset<T, PLAIN> *plain;
    RomRef(const FlashAsset<T, COMPRESSED> &b) : packed(&b), plain(nullptr) {}
    RomRef(const FlashAsset<T, PLAIN> &b) : packed(nullptr), plain(&b) {}
    RomRef(std::nullptr_t = nullptr) : packed(nullptr), plain(nullptr) {}
  };

private:
  template <typename T> class RomAsset {
  public:
    explicit RomAsset(const RomRef<T> &r)
        : packed(r.packed ? r.packed->packed : nullptr, r.packed ? r.packed->packedLen : 0,
                 r.packed ? r.packed->count : 0),
          plain(r.plain ? r.plain->data : nullptr),
          count(r.packed ? r.packed->count : (r.plain ? r.plain->count : 0)) {}

    // nullptr for an omitted asset
    const T *data() const { return plain ? plain : (count ? packed.data() : nullptr); }
    uint32_t size() const { return count; }

  private:
    Asset<T, COMPRESSED> packed;
    const T *plain;
    uint32_t count;
  };

  // ROM assets, unpacked into the Arena by the ctor (declared first: init order)
  RomAsset<uint32_t[8]> tile_rom;
  RomAsset<unsigned short> tile_map;
  RomAsset<unsigned char> main_rom;
  RomAsset<unsigned char> sprite_rom;
  RomAsset<unsigned char> sound_rom;
  RomAsset<unsigned char> lookup_prom;
  RomAsset<unsigned char> opcode_rom;
  RomAsset<unsigned char> bank_rom;

public:
  void reset() override;

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char opPrefixZ80(unsigned short Addr) override { return opZ80(Addr); }
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char inZ80(unsigned short Port) override;
  void outZ80(unsigned short Port, unsigned char Value) override;

  void run_frame(void) override;
  void renderAudio(int16_t *out, int n) override { Sn76489::renderBank(sn, out, n); }
  void prepare_frame(void) override;
  void render_row(short row) override;
  const int renderWidth() override;
  const int renderBuffer() override { return renderWidth() * 2 * STRIP_LINES; }

protected:
  // Omitted trailing fields in an initializer: absent
  struct Roms {
    RomRef<unsigned char> main_cpu;
    RomRef<unsigned char> sound_cpu;
    RomRef<uint32_t[8]> tiles;
    RomRef<unsigned short> tileMap;
    RomRef<unsigned char> sprites;
    RomRef<unsigned char> lookup;
    // encrypted main CPU: opcode image of 0000-7fff, romconv decrypts
    // main_cpu as data
    RomRef<unsigned char> opcodes;
    RomRef<unsigned char> banks;  // 16 KB banks at 8000-bfff (Config::bank)
    const uint16_t *palette;      // palette RAM byte -> RGB565, else system1_palette
  };
  // sound latch / video mode chip: 8255 PPI (sys1ppi) or Z80 PIO (sys1pio)
  enum Board : uint8_t { BOARD_PPI, BOARD_PIO };

  // ROT270: panel row = native x (column games). ROT0: panel row = native
  // line, a 240 px window into the 256 px line (landscape games, upright)
  enum Orientation : uint8_t { ROT270, ROT0 };

  // main ROM banking at 8000-bfff: none, or MAME bank44 (Heavy Metal):
  // bank = video mode bit 6, bit 2
  enum Bank : uint8_t { BANK_NONE, BANK_44 };

  struct Config {
    system1_video::Visarea vis;
    bool startButton2;  // START presses BUTTON2 (Regulus: bomb)
    Board board;        // omitted in an initializer: BOARD_PPI
    Orientation orientation;  // omitted: ROT270
    system1_video::Window window;  // ROT0 only
    Bank bank;          // omitted: BANK_NONE
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
  uint8_t readP1();
  uint8_t readSystem();
  bool takeSoundNmi();
  uint8_t ackSoundLatch();
  uint8_t videoMode() const;
  void selectBank();
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
