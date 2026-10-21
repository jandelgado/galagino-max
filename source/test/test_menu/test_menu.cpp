#include <stdio.h>
#include <string.h>
#include <unity.h>

// Menu scrolling and the remote build's menu attract. Logos are stubs: logo
// i decodes to one color, (i + 1) << 8, which only the highlighted logo
// shows (the others render grey), so the highlight is found by color.
// Arduino.h / FastLED.h / esp_attr.h in this directory are host stubs.
#define ASSETS_REMOTE
#include "../../src/emulation/arena.cpp"
#include "../../src/machines/menu/menu.cpp"

static const int COUNT = 10;

static uint32_t now;
uint32_t millis() { return now; }

static unsigned int keys;
unsigned int Input::buttons_get(void) { return keys; }

uint8_t hud_glyph_line(char, uint8_t) { return 0; }
// machineBase::reset(), never called
extern "C" void ResetZ80(Z80 *) {}
void Reset6502(M6502 *) {}

static const FlashAsset<uint8_t, COMPRESSED> blob = { };
static const Logo logos[COUNT] = { {blob}, {blob}, {blob}, {blob}, {blob},
                                   {blob}, {blob}, {blob}, {blob}, {blob} };

static uint16_t logoColor(int i) { return (uint16_t)((i + 1) << 8); }

void Logo::decodeInto(uint8_t *slot) const {
  uint16_t *palette = (uint16_t *)slot;
  for(uint32_t c = 0; c < COLORS; c++) {
    palette[c] = logoColor(this - logos);
  }
  memset(slot + COLORS * sizeof(uint16_t), 0, PIXELS / 2);
}

template<int I> static const Logo &logo() { return logos[I]; }
static const Logo &(*const logoFns[COUNT])() = { logo<0>, logo<1>, logo<2>, logo<3>, logo<4>,
                                                 logo<5>, logo<6>, logo<7>, logo<8>, logo<9> };

// uncached[i]: remote game i not downloaded yet
static bool uncached[COUNT];
static BundleRef refs[COUNT];
bool Loader::available(const BundleRef *ref) { return !ref || !uncached[ref - refs]; }

static machineInfo machines[COUNT];
static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static Menu *menu;

// highlighted logo seen in the last frame()
static int shown_idx;
static int shown_first;  // first and last screen line
static int shown_last;

static void open(int8_t shown, int8_t preselect) {
  for(int i = 0; i < COUNT; i++) {
    machines[i] = machineInfo();
    machines[i].logo = logoFns[i];
    machines[i].bundle = &refs[i];
  }
  delete menu;
  menu = new Menu(machines, COUNT, shown, preselect);
  menu->init(&input, frame_buffer, sprites, memory);
  keys = 0;
}

// one screen: input, then 36 strips, scanned for the highlight color
static void frame() {
  now += 33;
  menu->prepare_frame();
  shown_idx = -1;
  shown_first = -1;
  shown_last = -1;
  for(short row = 0; row < 36; row++) {
    menu->render_row(row);
    for(int y = 0; y < 8; y++) {
      for(int x = 0; x < 224; x++) {
        const uint16_t px = frame_buffer[y * 224 + x];
        // greys set red or green bits
        if(px == 0 || (px & 0xe0ff)) { continue; }
        const int idx = (px >> 8) - 1;
        TEST_ASSERT_TRUE_MESSAGE(shown_idx == -1 || shown_idx == idx, "one highlight");
        shown_idx = idx;
        if(shown_first < 0) { shown_first = row * 8 + y; }
        shown_last = row * 8 + y;
      }
    }
  }
}

static void assertFullyShown(int idx) {
  TEST_ASSERT_EQUAL_INT(idx, shown_idx);
  // line 0 may hide under the attract countdown bar
  TEST_ASSERT_TRUE_MESSAGE(shown_first >= 0 && shown_last - shown_first >= 94, "whole logo on screen");
}

static void assertCentred(int idx) {
  assertFullyShown(idx);
  TEST_ASSERT_EQUAL_INT(96, shown_first);
}

static void settle() {
  for(int i = 0; i < 16; i++) { frame(); }
}

void setUp(void) {
  static bool arena = false;
  if(!arena) {
    Arena::init();
    arena = true;
  }
  memset(uncached, 0, sizeof(uncached));
  now = 1000;
}

void tearDown(void) {
  delete menu;
  menu = nullptr;
}

// UP/DOWN faster than the scroll: the highlight never leaves the screen
static void test_fast_navigation_keeps_highlight_on_screen(void) {
  open(1, 1);
  settle();
  assertCentred(1);

  int sel = 1;
  for(int n = 0; n < 30; n++) {
    keys = BUTTON_DOWN;
    frame();
    sel = (sel + 1) % COUNT;
    assertFullyShown(sel);
    keys = 0;
    frame();
    assertFullyShown(sel);
  }
  for(int n = 0; n < 15; n++) {
    keys = BUTTON_UP;
    frame();
    sel = (sel + COUNT - 1) % COUNT;
    assertFullyShown(sel);
    keys = 0;
    frame();
    assertFullyShown(sel);
  }
  settle();
  assertCentred(sel);
}

// one logo: 8 screens
static void test_scroll_one_logo(void) {
  open(1, 1);
  settle();
  keys = BUTTON_DOWN;
  for(int n = 0; n < 7; n++) {
    frame();
    keys = 0;
    TEST_ASSERT_NOT_EQUAL(96, shown_first);
  }
  frame();
  assertCentred(2);
}

// game attract timeout preselects an uncached game: the menu shows the one
// the attract starts
static void test_preselect_skips_uncached(void) {
  uncached[5] = true;
  uncached[6] = true;
  open(4, 5);
  frame();
  assertFullyShown(7);
  settle();
  assertCentred(7);
}

// highlight left on an uncached game: attract moves it on, starts the game
// once centred
static void test_attract_scrolls_to_cached_game_first(void) {
  uncached[3] = true;
  open(2, 2);
  settle();
  keys = BUTTON_DOWN;
  frame();
  keys = 0;
  settle();
  assertCentred(3);

  now += MASTER_ATTRACT_MENU_TIMEOUT;
  int frames = 0;
  while(menu->selected() == Menu::NONE) {
    TEST_ASSERT_TRUE(++frames < 20);
    frame();
  }
  TEST_ASSERT_EQUAL_INT(4, menu->selected());
  assertCentred(4);
  TEST_ASSERT_TRUE(frames > 1);
}

static void test_attract_starts_cached_game_at_once(void) {
  open(2, 2);
  settle();
  now += MASTER_ATTRACT_MENU_TIMEOUT;
  frame();
  TEST_ASSERT_EQUAL_INT(2, menu->selected());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_fast_navigation_keeps_highlight_on_screen);
  RUN_TEST(test_scroll_one_logo);
  RUN_TEST(test_preselect_skips_uncached);
  RUN_TEST(test_attract_scrolls_to_cached_game_first);
  RUN_TEST(test_attract_starts_cached_game_at_once);
  return UNITY_END();
}
