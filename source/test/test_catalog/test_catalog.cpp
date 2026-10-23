#include <unity.h>

// catalog.h (romconv/romconv/remote/catalog.py) in a mode 3 build with only mspacman built
// in: pacman's assets stay in the app, gng is compiled in but remote.
#define ASSETS_REMOTE
#define ENABLE_MSPACMAN
#include "../../src/catalog.h"

#ifndef BUILTIN_PACMAN
#error "mspacman built in needs pacman's assets in the app"
#endif
#ifdef BUILTIN_GNG
#error "gng not enabled, its assets are remote"
#endif
#ifndef GAME_GNG
#error "mode 3 compiles every catalog game"
#endif
#ifdef GAME_MOTORACE
#error "motorace is not in the catalog"
#endif
#ifndef BUILTIN_WILLIAMS
#error "shared dirs are always built in"
#endif

void setUp(void) {}
void tearDown(void) {}

static void test_catalog_macros(void) {
  TEST_PASS();
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_catalog_macros);
  return UNITY_END();
}
