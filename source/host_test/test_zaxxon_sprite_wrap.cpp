// Regression test: zaxxon sprites near the dst_x-axis wrap edge must still
// resolve to an on-screen pixel, not just vanish until they drift into the
// unwrapped range (root-caused 2026-09-23, see zaxxon_sprite_geom.h).
#include <cassert>
#include <cstdio>
#include "../src/machines/zaxxon/zaxxon_sprite_geom.h"

static const int16_t SCREEN_WIDTH = 224;

void test_unwrapped_position_resolves_directly() {
  // spr_x=16, oy=0 -> dst_x = 239-16-0 = 223, already in range without
  // needing the -256 wrap copy.
  assert(zaxxon_resolve_sprite_dst_x(16, 0, SCREEN_WIDTH) == 223);
  printf("1. unwrapped position resolves directly: OK\n");
}

void test_wrapped_position_resolves_via_minus_256() {
  // find_minimum_y() wraps a slightly-off-top-edge sprite to spr_x near
  // 255 (e.g. 250) instead of a small negative value. Unwrapped:
  // dst_x = 239-250-oy, always negative -> every pixel would clip.
  // Wrapped: (250-256)=-6, dst_x = 239-(-6)-oy = 245-oy, in [0,224) for
  // oy in [22,31] -- exactly the sliver of the sprite that should be
  // peeking onto the bottom of the screen.
  const uint8_t spr_x = 250;
  assert(zaxxon_resolve_sprite_dst_x(spr_x, 30, SCREEN_WIDTH) == 245 - 30);
  assert(zaxxon_resolve_sprite_dst_x(spr_x, 31, SCREEN_WIDTH) == 245 - 31);
  printf("2. wrapped position resolves via -256 copy: OK\n");
}

void test_fully_offscreen_position_returns_invisible() {
  // spr_x=240: spr_x+oy spans [240,271] for oy 0-31, the gap between the
  // unwrapped visible window [16,239] and the wrapped one [272,495] --
  // neither copy lands any oy inside the visible range, so a sprite
  // genuinely off both edges stays fully clipped.
  for (uint16_t oy = 0; oy < 32; oy++) {
    assert(zaxxon_resolve_sprite_dst_x(240, oy, SCREEN_WIDTH) == -1);
  }
  printf("3. fully off-screen position stays invisible: OK\n");
}

int main() {
  test_unwrapped_position_resolves_directly();
  test_wrapped_position_resolves_via_minus_256();
  test_fully_offscreen_position_returns_invisible();
  printf("\nALL ZAXXON SPRITE WRAP TESTS PASSED\n");
  return 0;
}
