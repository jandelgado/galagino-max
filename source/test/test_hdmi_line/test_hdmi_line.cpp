#include <unity.h>
#include "../../src/hal/rp2350/hdmi_line.h"

using namespace hal;
using namespace hal::hdmi;

void setUp(void) {}
void tearDown(void) {}

static const uint32_t H_TOTAL = 800;

struct Span {
  uint32_t start;  // pixel clock
  uint32_t clocks;
};

// Walks an HSTX command list: total clocks, and where the raw island is.
static uint32_t walk(const uint32_t *cmds, uint32_t words, Span *island) {
  uint32_t clock = 0;
  uint32_t i = 0;
  while (i < words) {
    const uint32_t cmd = cmds[i] & 0xf000;
    const uint32_t n = cmds[i] & 0xfff;
    i++;
    if (cmd == hstx::CMD_RAW_REPEAT) {
      i++;
    } else if (cmd == hstx::CMD_RAW) {
      island->start = clock;
      island->clocks = n;
      i += n;
    } else if (cmd == hstx::CMD_TMDS) {
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(words, i, "pixels follow the TMDS command");
    } else {
      TEST_ASSERT_EQUAL_HEX32(hstx::CMD_NOP, cmd);
      continue;
    }
    if (cmd != hstx::CMD_NOP) {
      clock += n;
    }
  }
  return clock;
}

static void assertLine(Line kind, VSync v, uint32_t expectWords) {
  uint32_t cmds[ACTIVE_WORDS] = {0};
  TEST_ASSERT_EQUAL_UINT32(expectWords, lineHeader(cmds, kind, v));
  TEST_ASSERT_EQUAL_HEX32(hstx::CMD_RAW | ISLAND_WORDS, cmds[ISLAND_AT - 1]);

  Span island = {0, 0};
  TEST_ASSERT_EQUAL_UINT32(H_TOTAL, walk(cmds, expectWords, &island));

  // island and its preamble inside the hsync pulse
  TEST_ASSERT_EQUAL_UINT32(ISLAND_WORDS, island.clocks);
  TEST_ASSERT_EQUAL_UINT32(hstx::H_FRONT_PORCH + W_PREAMBLE, island.start);
  TEST_ASSERT_TRUE(island.start + island.clocks <= hstx::H_FRONT_PORCH + hstx::H_SYNC_WIDTH);
}

static void test_blank_line(void) {
  assertLine(Line::BLANK, VSync::OFF, BLANK_WORDS);
  assertLine(Line::BLANK, VSync::PULSE, BLANK_WORDS);
}

static void test_active_line(void) {
  assertLine(Line::ACTIVE, VSync::OFF, ACTIVE_WORDS);

  // video preamble and guard band right before the pixels
  uint32_t cmds[ACTIVE_WORDS];
  lineHeader(cmds, Line::ACTIVE, VSync::OFF);
  TEST_ASSERT_EQUAL_HEX32(hstx::CMD_RAW_REPEAT | W_VIDEO_GUARD, cmds[ACTIVE_WORDS - 3]);
  TEST_ASSERT_EQUAL_HEX32(VIDEO_GUARD, cmds[ACTIVE_WORDS - 2]);
  TEST_ASSERT_EQUAL_HEX32(VIDEO_PREAMBLE, cmds[ACTIVE_WORDS - 5]);
}

static void test_sync_levels(void) {
  uint32_t cmds[BLANK_WORDS];
  lineHeader(cmds, Line::BLANK, VSync::PULSE);
  TEST_ASSERT_EQUAL_HEX32(hstx::SYNC_V0_H1, cmds[1]);
  TEST_ASSERT_EQUAL_HEX32(DATA_PREAMBLE_V0, cmds[4]);
  TEST_ASSERT_EQUAL_HEX32(hstx::SYNC_V0_H0, cmds[ISLAND_AT + ISLAND_WORDS + 2]);
  lineHeader(cmds, Line::BLANK, VSync::OFF);
  TEST_ASSERT_EQUAL_HEX32(hstx::SYNC_V1_H1, cmds[1]);
  TEST_ASSERT_EQUAL_HEX32(DATA_PREAMBLE_V1, cmds[4]);
  TEST_ASSERT_EQUAL_HEX32(hstx::SYNC_V1_H0, cmds[ISLAND_AT + ISLAND_WORDS + 2]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_blank_line);
  RUN_TEST(test_active_line);
  RUN_TEST(test_sync_levels);
  return UNITY_END();
}
