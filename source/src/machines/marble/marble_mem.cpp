#include "marble_mem.h"

#include "../../catalog.h"
#ifdef GAME_MARBLE

#include <stdio.h>
#include <stdlib.h>
#include "../../emulation/arena.h"
#include "../../emulation/asset.h"
#include "marble_rom.h"

const uint8_t *marble_tables = nullptr;  // loadRom(): marble_tables_blob in the Arena

namespace marble_physics {

static const uint8_t RACES = 6;
static const uint8_t NO_RACE = 0xff;
static const uint8_t PAIR_BYTES = 54;  // 44 fields of 10 bits per diagonal pair
static const uint8_t FIELDS = 44;      // field 43 (column 21, odd diagonal) not stored

// Arena buffers of loadRom(): physics reads them every tick, flash keeps them LZMA packed
static uint8_t *prog = nullptr;         // marble_prog: all program ROM blocks
static uint8_t *terrainBuf = nullptr;   // bases of loadedRace (maxd words), then its index
static uint8_t loadedRace = NO_RACE;

void fail(const char *what, uint32_t addr) {
  printf("marble physics: %s access at %06x\n", what, (unsigned)addr);
  abort();
}

// marble_tables in flash (PLAIN) or unpacked into its planned Arena block,
// as romconv's Arena plan picks per layout
static const uint8_t *loadTables(const FlashAsset<unsigned char, PLAIN> &b) {
  return Asset<unsigned char, PLAIN>(b).data();  // resolves remote blobs
}

static const uint8_t *loadTables(const FlashAsset<unsigned char, COMPRESSED> &b) {
  const Asset<unsigned char, COMPRESSED> t(b);
  uint8_t *tables = Arena::alloc(t.size(), alignof(uint32_t), b.block);
  t.decodeInto(tables);
  return tables;
}

void loadRom() {
  const Asset<unsigned char, COMPRESSED> p(marble_prog_blob);
  prog = Arena::alloc<uint8_t>(p.size());
  p.decodeInto(prog);

  marble_tables = loadTables(marble_tables_blob);

  terrainBuf = Arena::alloc(MARBLE_TERRAIN_MAX, alignof(uint16_t));
  loadedRace = NO_RACE;
}

void Mem::setRace(uint8_t race) {
  if (race >= RACES) { fail("race", race); }
  if (!terrainBuf) { fail("ROM not loaded, race", race); }
  terrain = &marble_terrain[race];
  this->race = race;
  if (race == loadedRace) { return; }

  // once per race start: about 6 KB LZMA
  const Asset<unsigned char, COMPRESSED> t(terrain->packed);
  t.decodeInto(terrainBuf);
  loadedRace = race;
}

// the terrain buffer holds one race: a Mem of the race before a switch must not read it
void Mem::checkRace() const {
  if (race != loadedRace) { fail("terrain of unloaded race", race); }
}

uint8_t Mem::romb(uint32_t a) const {
  // blocks are sorted by address and do not overlap: last block starting at or below a
  uint8_t lo = 0, hi = MARBLE_PROG_BLOCKS;
  while (lo < hi) {
    const uint8_t mid = (lo + hi) / 2;
    if (marble_prog_blocks[mid].addr <= a) { lo = mid + 1; } else { hi = mid; }
  }
  if (lo) {
    const MarbleProgBlock &b = marble_prog_blocks[lo - 1];
    if (a - b.addr < b.len) { return prog[b.off + a - b.addr]; }
  }
  fail("ROM", a);
}

uint16_t Mem::maxd() const { return terrain->maxd; }

const MarbleTerrainInfo &Mem::desc() const { return *terrain; }

int16_t Mem::edgeFrom(uint8_t col, uint8_t d) const {
  const uint16_t ult = rw(ULT_COL);
  const uint16_t frame = ult / 0x90;
  if (col >= 36 || ult % 0x90 || frame >= terrain->nedges) { fail("terrain edges", ult); }

  // ascending list ending in maxd - 1: the scan stops there for d < maxd
  const uint8_t *p = marble_edge_lists() + terrain->edges()[36 * frame + col];
  while (*p < d) { p++; }
  return *p;
}

const MarbleSpriteSet &spriteSet(uint8_t level) {
  if (level >= RACES) { fail("race", level); }
  return marble_sprites[marble_sprite_race[level]];
}

// frame id of the frame header at hdr in s (addr ascending), aborts with `what` if none
static uint8_t frameOf(const MarbleSpriteSet &s, uint32_t hdr, const char *what) {
  static const uint32_t FRAMES_BASE = 0x20000;  // frame headers: 0x20000 + addr * 2
  const uint32_t key = (hdr - FRAMES_BASE) >> 1;
  const uint16_t *addr = s.addr();
  uint8_t lo = 0, hi = s.nhdr;
  while (lo < hi) {
    const uint8_t mid = (lo + hi) / 2;
    if (addr[mid] < key) { lo = mid + 1; } else { hi = mid; }
  }
  if (hdr < FRAMES_BASE || lo == s.nhdr || addr[lo] != key) { fail(what, hdr); }
  return lo;
}

uint8_t Mem::spriteFrame(uint32_t hdr) const {
  return frameOf(spriteSet(rw(RACE)), hdr, "sprite frame");
}

const MarbleSpriteSet &deathSet() { return marble_death_set; }

uint8_t deathFrame(uint32_t hdr) { return frameOf(marble_death_set, hdr, "death frame"); }

int16_t Mem::stripFrame(int16_t i) const {
  static const uint8_t STRIP_N = 23;
  const MarbleSpriteSet &s = spriteSet(rw(RACE));
  if (i < 0 || i >= STRIP_N || s.nhdr + i >= s.nframes || !s.frames()[s.nhdr + i].ncols) { return -1; }
  return s.nhdr + i;
}

uint16_t Mem::base(int16_t d) const {
  if (d < 0 || d >= terrain->maxd) { fail("terrain base", (uint16_t)d); }
  checkRace();
  return ((const uint16_t *)terrainBuf)[d];
}

// 10 bit fields MSB first, field f at bit 10 * f of the pair (0x1a9cc layout).
uint16_t Mem::vertexCode(int16_t d, int16_t f) const {
  if (d < 0 || d >= terrain->maxd) { fail("terrain diagonal", (uint16_t)d); }
  if (f < 0 || f >= FIELDS) { fail("terrain field", (uint16_t)f); }

  checkRace();

  uint16_t idx = 0;
  if (f < FIELDS - 1) {
    const uint8_t *index = terrainBuf + 2 * terrain->maxd;
    const uint8_t *p = index + PAIR_BYTES * (d >> 1) + 10 * f / 8;
    idx = ((p[0] << 8 | p[1]) >> (6 - 10 * f % 8)) & 0x3ff;
  }
  if (idx >= terrain->ncodes) { fail("terrain code index", idx); }
  return terrain->codes()[idx];
}

uint8_t Mem::record(uint16_t code, uint8_t q) const {
  const uint32_t i = (uint32_t)code + q;
  if (i >= terrain->nrecords) { fail("terrain record", i); }
  return terrain->records()[i];
}

}  // namespace marble_physics

#endif  // GAME_MARBLE
