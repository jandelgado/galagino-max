#ifndef MARBLE_MEM_H
#define MARBLE_MEM_H

#include <stdint.h>

struct MarbleTerrainInfo;
struct MarbleSpriteSet;

// 68010 memory as the marble physics sees it (MARBLE.md "Marble physics", "Remake data"):
//
//   0x400000-0x401fff  work RAM, big-endian image in a caller buffer (rb/rw/rl, wb/ww/wl)
//   0x000000-0x02ffff  program ROM data: tables by index (marble_rom.h, MarbleTable) and the
//                      accessors below for data the work RAM points to by 68010 address
//   video RAM, slapstic terrain of the race: vertex codes, base heights, records
//
// Any access outside these aborts with the address: a missing table entry or a port bug.
namespace marble_physics {

static const uint32_t RAM_BASE = 0x400000;
static const uint16_t RAM_SIZE = 0x2000;
static const uint32_t FRAMES = 0x400010;  // long, logic tick counter (vblank 0x287a6)

// shared work RAM globals
static const uint32_t RACE = 0x400394;      // word
static const uint32_t ULT_COL = 0x40045c;   // word: ultimate race frame * 0x90
static const uint32_t DYN = 0x40076e;       // 128 dynamic terrain codes (words)
static const uint32_t CAMERA_L = 0x40097c;  // camera long (0x13ee6)
static const uint32_t CAMERA = 0x40097e;    // camera word (low word of CAMERA_L)
// BIOS message slot 0 (0x12a, attract demo banners): string chain long (0: none), state byte,
// frames shown word, frames counted word
static const uint32_t MSG_CHAIN = 0x401f04, MSG_STATE = 0x401f1c, MSG_FRAMES = 0x401f20;
static const uint32_t MSG_COUNT = 0x401f28;

inline int8_t s8(uint32_t v) { return (int8_t)v; }
inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p) << 16 | be16(p + 2); }
inline int16_t s16(uint32_t v) { return (int16_t)v; }

[[noreturn]] void fail(const char *what, uint32_t addr);

// Unpacks marble_tables to the Arena (local builds) and reserves the terrain buffer. Once
// per machine instance, before the first Mem: the buffers die with the Arena.
void loadRom();

class Mem {
public:
  Mem(uint8_t *ram, uint8_t race) : ram(ram) { setRace(race); }
  // unpacks the race terrain into the shared buffer unless loaded: Mems of another race fail
  void setRace(uint8_t race);

  uint8_t rb(uint32_t a) const { return ram[off(a)]; }
  uint16_t rw(uint32_t a) const { return rb(a) << 8 | rb(a + 1); }
  uint32_t rl(uint32_t a) const { return (uint32_t)rw(a) << 16 | rw(a + 2); }
  int8_t sb(uint32_t a) const { return (int8_t)rb(a); }
  int16_t sw(uint32_t a) const { return (int16_t)rw(a); }
  int32_t sl(uint32_t a) const { return (int32_t)rl(a); }
  void wb(uint32_t a, uint8_t v) { ram[off(a)] = v; }
  void ww(uint32_t a, uint16_t v) { wb(a, v >> 8); wb(a + 1, (uint8_t)v); }
  void wl(uint32_t a, uint32_t v) { ww(a, v >> 16); ww(a + 2, (uint16_t)v); }

  // Terrain of the race: diagonal d (0 .. maxd() - 1), field f = column + 22 on odd
  // diagonals (Terrain.index), quadrant q of a record code (descriptor +0x00).
  uint16_t maxd() const;
  uint16_t base(int16_t d) const;
  uint16_t vertexCode(int16_t d, int16_t f) const;
  uint8_t record(uint16_t code, uint8_t q) const;
  const MarbleTerrainInfo &desc() const;  // top, range, cell of the race
  // 0x264aa terrain edge: first diagonal >= d (d < maxd) of screen column col with an edge,
  // in the table of the current ultimate scroll frame (ULT_COL). Every column ends in maxd - 1.
  int16_t edgeFrom(uint8_t col, uint8_t d) const;

  // Sprite frames of the race (RACE word). spriteFrame: frame id of the frame header at 68010
  // address hdr, aborts if there is none. stripFrame: wave strip entry i (0x27fa2) as frame id,
  // -1 if it has no columns or no entry.
  uint8_t spriteFrame(uint32_t hdr) const;
  int16_t stripFrame(int16_t i) const;

private:
  static uint16_t off(uint32_t a) {
    if (a - RAM_BASE >= RAM_SIZE) { fail("RAM", a); }
    return (uint16_t)(a - RAM_BASE);
  }

  void checkRace() const;

  uint8_t *ram;
  const MarbleTerrainInfo *terrain = nullptr;
  uint8_t race = 0;
};

// Program ROM data by 68010 address (the work RAM holds these pointers; marble_rom.h spans).
// animEntry: long of an animation list (0xffffffff ends a list; mode 1/2 entries are two
// longs). frameBox: frame header bytes +4..+7 (dx, dy, sx, sy); the empty animation's
// 0xffffffff header reads zeros (the arcade reads BIOS vectors there). framePos: bytes +0..+3
// (x, y, width, height), slinky frames only.
uint32_t animEntry(uint32_t p);
const uint8_t *frameBox(uint32_t hdr);
const uint8_t *framePos(uint32_t hdr);
// scriptWord/Long: level object byte code at pc. dynWord: word k of a terrain code block
// (0x13628: slot, count, words) at p, reached from mode 1/2 animation entries or dyn_init.
uint16_t scriptWord(uint32_t pc);
uint32_t scriptLong(uint32_t pc);
uint16_t dynWord(uint32_t p, uint16_t k);
// occluderWord: word of the explicit occluder tables and records of 0x264aa at a (the lists
// overlap by whole words, so they stay 68010 addressed: marble_occluder_tabs[column] + offsets)
uint16_t occluderWord(uint32_t a);
// pathByte: byte k of a record at p of a path: steelie node lists (6 bytes: cell x, y, 4
// links), slinky and acid waypoints (4), attract demo waypoints (4); 0xff ends a list.
uint8_t pathByte(uint32_t p, uint8_t k);
uint32_t demoWaypoints(uint8_t race);   // first waypoint record of an attract demo race
uint16_t pistonTimer(uint32_t p, uint8_t k);   // word k of the piston variant timer table at p

// sprite set of level n (races share sets), for the renderer
const MarbleSpriteSet &spriteSet(uint8_t level);

// Player death frames of every race (dizzy, eaten, shatter and its overlay, dissolve), for
// the renderer. deathFrame: frame id of the frame header at 68010 address hdr (rl(rl(+0x5a))
// or rl(rl(+0x62))), aborts if there is none.
const MarbleSpriteSet &deathSet();
uint8_t deathFrame(uint32_t hdr);

}  // namespace marble_physics

#endif
