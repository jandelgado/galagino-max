#ifndef SYSTEM1_COLLISION_H
#define SYSTEM1_COLLISION_H

#include <stdint.h>
#include "system1_video.h"

// Sega System 1 collision hardware (MAME system1_v.cpp). The game polls it
// at 0xf000-0xffff to detect hits, so it must exist even though galagino
// renders elsewhere. Scanned once per frame at vblank from live RAM:
//   sprite vs sprite: spr[earlier + 32 * later], 32 x 32 bits
//   sprite vs tiles:  mix[bank * 32 + sprite] via lookup PROM bit 2
class Collision {
public:
  struct Frame {
    const uint8_t *spriteRam;
    const uint8_t *spriteRom;
    uint8_t spriteBanks;
    const uint8_t *vram;
    system1_video::TileSet tiles;
    const uint8_t *prom;
    uint8_t videoMode;
    system1_video::Visarea vis;
  };

  void reset();
  void scan(const Frame &f);

  uint8_t mixRead(uint16_t addr) const { return mix[addr & MIX_MASK] | IDLE_BITS | (mixSummary << 7); }
  void mixClear(uint16_t addr) { mix[addr & MIX_MASK] = 0; }
  void mixReset() { mixSummary = 0; }
  uint8_t sprRead(uint16_t addr) const { return spr[addr & SPR_MASK] | IDLE_BITS | (sprSummary << 7); }
  void sprClear(uint16_t addr) { spr[addr & SPR_MASK] = 0; }
  void sprReset() { sprSummary = 0; }
  bool hit() const { return mixSummary || sprSummary; }

private:
  static constexpr uint16_t MIX_MASK = 0x3f;
  static constexpr uint16_t SPR_MASK = 0x3ff;
  static constexpr uint8_t IDLE_BITS = 0x7e;
  static constexpr int LINE_UNITS = 512;
  static constexpr int LINE_WORDS = LINE_UNITS / 32;

  void drawLine(const Frame &f, const system1_video::Sprite *list, int count, int y);
  void mixLine(const Frame &f, int y, int scroll, uint8_t yscroll, uint16_t pairs);

  uint8_t mix[MIX_MASK + 1];
  uint8_t spr[SPR_MASK + 1];
  uint8_t mixSummary;
  uint8_t sprSummary;
  // line buffer: sprite number per 512 unit, valid where its occupied bit is
  // set; mixLine visits set bits only (SWAT: ~55 of 512 units per line)
  int8_t line[LINE_UNITS];
  uint32_t occupied[LINE_WORDS];
};

#endif
