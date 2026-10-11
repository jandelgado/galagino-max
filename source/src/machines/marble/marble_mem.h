#ifndef MARBLE_MEM_H
#define MARBLE_MEM_H

#include <stdint.h>

struct MarbleTerrainInfo;

// 68010 memory as the marble physics sees it (MARBLE.md "Marble physics", "Remake data"):
//
//   0x400000-0x401fff  work RAM, big-endian image in a caller buffer (rb/rw/rl, wb/ww/wl)
//   0x000000-0x02ffff  program ROM, only the bytes in marble_prog_blocks (romb/romw/roml)
//   video RAM, slapstic terrain of the race: vertex codes, base heights, records
//
// Any access outside these aborts with the address: a missing ROM block or a port bug.
namespace marble_physics {

static const uint32_t RAM_BASE = 0x400000;
static const uint16_t RAM_SIZE = 0x2000;
static const uint32_t FRAMES = 0x400010;  // long, 60 Hz counter (vblank 0x287a6)

// shared work RAM globals
static const uint32_t RACE = 0x400394;      // word
static const uint32_t ULT_COL = 0x40045c;   // word: ultimate race frame * 0x90
static const uint32_t DYN = 0x40076e;       // 128 dynamic terrain codes (words)
static const uint32_t CAMERA_L = 0x40097c;  // camera long (0x13ee6)
static const uint32_t CAMERA = 0x40097e;    // camera word (low word of CAMERA_L)

// terrain descriptor (Mem::desc()) fields, program ROM
static const uint32_t DESC_TOP = 0x10, DESC_RANGE = 0x12, DESC_CELL = 0x14, DESC_END = 0x18;

inline int8_t s8(uint32_t v) { return (int8_t)v; }
inline int16_t s16(uint32_t v) { return (int16_t)v; }

[[noreturn]] void fail(const char *what, uint32_t addr);

class Mem {
public:
  Mem(uint8_t *ram, uint8_t race) : ram(ram) { setRace(race); }
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

  uint8_t romb(uint32_t a) const;
  uint16_t romw(uint32_t a) const { return romb(a) << 8 | romb(a + 1); }
  uint32_t roml(uint32_t a) const { return (uint32_t)romw(a) << 16 | romw(a + 2); }

  // Terrain of the race: diagonal d (0 .. maxd() - 1), field f = column + 22 on odd
  // diagonals (Terrain.index), quadrant q of a record code (descriptor +0x00).
  uint16_t maxd() const;
  uint16_t base(int16_t d) const;
  uint16_t vertexCode(int16_t d, int16_t f) const;
  uint8_t record(uint16_t code, uint8_t q) const;
  uint32_t desc() const;
  // 0x264aa terrain edge: first diagonal >= d (d < maxd) of screen column col with an edge,
  // in the table of the current ultimate scroll frame (ULT_COL). -1: none.
  int16_t edgeFrom(uint8_t col, uint8_t d) const;

private:
  static uint16_t off(uint32_t a) {
    if (a - RAM_BASE >= RAM_SIZE) { fail("RAM", a); }
    return (uint16_t)(a - RAM_BASE);
  }

  uint8_t *ram;
  const MarbleTerrainInfo *terrain = nullptr;
};

}  // namespace marble_physics

#endif
