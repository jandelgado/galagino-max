#ifndef MOTORACE_H
#define MOTORACE_H

#include "../machineBase.h"
#include "../../emulation/ay8910.h"
#include "../../emulation/seqlock.h"

#include "../../catalog.h"
#ifdef GAME_MOTORACE

#include "motorace_logo.h"
#include "motorace_dipswitches.h"
#include "../../cpus/m6803/m6803.h"

// MotoRace USA memory layout in `memory[]` buffer (RAMSIZE=9344):
//   VRAM: 0x8000-0x8FFF (4096) -> offset 0x0000
//   Sprite RAM: 0xC800-0xC9FF (512) -> offset 0x1000
//   Work RAM: 0xE000-0xEFFF (4096) -> offset 0x1200
#define MR_MEM_VRAM     0x0000
#define MR_MEM_SPRITES  0x1000
#define MR_MEM_WORKRAM  0x1200

class motorace : public machineBase
{
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> motorace_rom;
  Asset<unsigned char, COMPRESSED> motorace_snd_rom;
  Asset<uint32_t[32], COMPRESSED> motorace_spritemap;
  Asset<uint32_t[8], COMPRESSED> motorace_tilemap;
public:
  motorace();

  uint8_t machineType()    override { return MCH_MOTORACE; }
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
  signed char useVideoHalfRate() override { return 0; }
  //bool hasOpaqueBG()           override { return true;  } // BG scroll opaco (no memset richiesto)

  const int renderWidth()  override { return 240; }
  const int renderBuffer() override { return 240 * 2 * 8; }

  unsigned char rdZ80(unsigned short Addr) override;
  void          wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;

  void init(Input *input, unsigned short *framebuffer,
            sprite_S *spritebuffer, unsigned char *memorybuffer) override;
  void reset() override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  static const Logo &logo(void);

  // M6803 sound CPU memory access (chiamati da callback C)
  uint8_t snd_read(uint16_t addr);
  void    snd_write(uint16_t addr, uint8_t val);
  void    snd_port_write(uint8_t port, uint8_t val);
  uint8_t snd_port_read(uint8_t port);

protected:
  // Unused: render_row() draws strips directly.
  void blit_tile(short row, char col)            override { }
  void blit_sprite(short row, unsigned char s)   override { }

private:
  void draw_tiles(short strip);
  void draw_sprite(short strip, unsigned char s);
  void publish_video(void);
  static int window_col(int row, int scroll);

  static const int FB_W = 240;
  static const int SCREEN_LINES = 240;
  static const int TOP_LINE = 24;         // (288 - 240) / 2
  static const int VIS_X_MAX = 247;       // native x of screen line 0
  static const int MAP_COLS = 64;
  static const int MAP_ROWS = 32;
  static const int MAP_W = MAP_COLS * 8;
  static const int SCROLL_ROWS = 24;      // tilemap rows 24-31: fixed HUD
  static const int TUNNEL_COLOR = 0x0F;
  static const int TUNNEL_PEN = 6;        // tunnel pens 6, 7 cover sprites
  static const int SPRITE_SIZE = 16;
  // Panel: 176 of the 192 playfield columns (screen x 0-191, tilemap rows
  // 0-23), panned to follow the player, then the HUD (screen x 192-255).
  static const int PLAYFIELD_W = SCROLL_ROWS * 8;
  static const int HUD_W = MAP_ROWS * 8 - PLAYFIELD_W;
  static const int VIEW_W = FB_W - HUD_W;
  static const int MAX_PAN = PLAYFIELD_W - VIEW_W;
  static const int PLAYER_X = 0x303;      // work RAM: player screen x
  static const unsigned char FLIP_ROWS = 1;
  static const unsigned char FLIP_COLS = 2;

  static const int WINDOW_COLS = 32;      // 240 visible native x span 32 tiles
  static const int SPRITE_RAM = 0x200;

  // per strip column: bit k set = line k shows a tunnel pen over sprites
  unsigned char front[FB_W];
  int pan = 0;                            // first playfield column shown

  // Render input latched at vblank: run_frame() and render run on two
  // cores, live RAM mixes two emulated frames (sprites vs scroll). Tiles:
  // the 32 tilemap columns the scroll shows (window starts at tile
  // scroll / 8, 0 for the HUD rows).
  struct VideoState {
    unsigned char tiles[MAP_ROWS][WINDOW_COLS * 2];
    unsigned char sprites[SPRITE_RAM];
    uint16_t scroll;
    unsigned char player;
  };
  Seqlock<VideoState> vblank;
  VideoState video = {};

  unsigned char scroll_x_low;
  unsigned char scroll_x_high;
  unsigned char flipscreen;
  uint16_t boot_frames;
  unsigned char sound_cmd;

  // M6803 sound CPU state
  m6803_state   snd_cpu;
  unsigned char snd_port1;
  unsigned char snd_port2;
  unsigned char ay_addr[2];
  unsigned char ay_regs[2][16];

  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *rom_ptr = nullptr;
  const unsigned char *snd_rom_ptr = nullptr;

private:
  static const uint8_t AY_INC = 5;
  static const uint8_t AY_GAIN = 5;
  Ay8910 ay[2] = {Ay8910(AY_INC, AY_GAIN, Ay8910::NO_NOISE), Ay8910(AY_INC, AY_GAIN, Ay8910::NO_NOISE)};
};

// Global pointer per callback M6803
extern motorace *g_motorace_instance;

#endif // GAME_MOTORACE
#endif // MOTORACE_H
