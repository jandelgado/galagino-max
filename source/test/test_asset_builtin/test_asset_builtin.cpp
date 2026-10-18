#include <unity.h>

// Generated asset headers include only asset.h, and the machine .cpp that
// includes them may never include catalog.h: asset.h must bring BUILTIN_<DIR>,
// or a built-in game compiles its assets in the remote form (device bug:
// pacman aborted with "remote asset missing").
#define ASSETS_REMOTE
#define ENABLE_PACMAN
#include "../../src/emulation/asset.h"

#ifndef BUILTIN_PACMAN
#error "asset.h must define BUILTIN_ for built-in games"
#endif

void setUp(void) {}
void tearDown(void) {}

static void test_builtin_seen_by_asset_headers(void) {
  TEST_PASS();
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_builtin_seen_by_asset_headers);
  return UNITY_END();
}
