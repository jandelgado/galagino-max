#include <unity.h>

// LoaderScreen ends every redraw as a full screen: DVI drops strips past 36
// until endFrame(), so redraws after the first never showed.
#define ASSETS_REMOTE
#include "../../src/ui/loader_screen.cpp"

static const uint8_t STRIPS = 36;

unsigned int Input::buttons_get(void) { return 0; }
void hud_text(uint16_t *, uint16_t, uint8_t, uint8_t, uint16_t, uint16_t, uint16_t, const char *) {}

void setUp(void) {}
void tearDown(void) {}

static void test_every_redraw_ends_a_screen(void) {
  hal::Video video;
  uint16_t frameBuffer[240 * 8];
  Input input;
  LoaderScreen screen(video, frameBuffer, input);

  // the game before left half rate on
  video.setHalfRate(true);
  screen.begin("PACMAN");
  screen.line("ERASE");
  screen.line("WIFI OK");

  TEST_ASSERT_EQUAL_UINT32(3 * STRIPS, video.submits());
  TEST_ASSERT_EQUAL_UINT32(3, video.endFrames());
  TEST_ASSERT_EQUAL_UINT8(1, video.screenParts());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_every_redraw_ends_a_screen);
  return UNITY_END();
}
