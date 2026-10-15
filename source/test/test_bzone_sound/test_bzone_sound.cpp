#include <string.h>
#include <unity.h>

#include "../../src/emulation/discrete.cpp"
#include "../../src/machines/bzone/bzone_sound.cpp"

static BzoneSound snd;
static int16_t pokey[4800], out[4800];

void setUp(void) {
  snd.reset();
  memset(pokey, 0, sizeof(pokey));
}
void tearDown(void) {}

static int peak(int n) {
  int p = 0;
  for (int i = 0; i < n; i++) {
    TEST_ASSERT_TRUE(out[i] <= 511 && out[i] >= -511);
    p = out[i] > p ? out[i] : (-out[i] > p ? -out[i] : p);
  }
  return p;
}

// sound enable low mutes everything, POKEY included
static void test_sound_disabled_is_silent(void) {
  for (int i = 0; i < 4800; i++) {
    pokey[i] = (i & 16) ? 400 : -400;
  }
  snd.render(BZ_MOTOR_EN | BZ_EXPLO | BZ_SHELL, pokey, out, 4800);
  TEST_ASSERT_EQUAL(0, peak(4800));
}

static void test_pokey_passes(void) {
  for (int i = 0; i < 4800; i++) {
    pokey[i] = (i & 16) ? 400 : -400;
  }
  snd.render(BZ_SOUND_EN, pokey, out, 4800);
  TEST_ASSERT_TRUE(peak(4800) > 20);
}

// engine runs only with motor enable, revving raises its pitch
static void test_engine(void) {
  snd.render(BZ_SOUND_EN, pokey, out, 4800);
  TEST_ASSERT_TRUE(peak(4800) < 5);
  snd.render(BZ_SOUND_EN | BZ_MOTOR_EN, pokey, out, 4800);
  TEST_ASSERT_TRUE(peak(4800) > 20);
}

// explosion: enable pulse, then a decaying noise burst
static void test_explosion_decays(void) {
  snd.render(BZ_SOUND_EN | BZ_EXPLO | BZ_EXPLO_LOUD, pokey, out, 480);
  snd.render(BZ_SOUND_EN | BZ_EXPLO_LOUD, pokey, out, 4800);
  const int early = peak(1200);
  snd.render(BZ_SOUND_EN | BZ_EXPLO_LOUD, pokey, out, 4800);
  snd.render(BZ_SOUND_EN | BZ_EXPLO_LOUD, pokey, out, 4800);
  TEST_ASSERT_TRUE(early > 20);
  TEST_ASSERT_TRUE(peak(4800) < early);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_sound_disabled_is_silent);
  RUN_TEST(test_pokey_passes);
  RUN_TEST(test_engine);
  RUN_TEST(test_explosion_decays);
  return UNITY_END();
}
