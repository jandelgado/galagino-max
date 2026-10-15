#include <new>
#include <stdio.h>
#include <string.h>
#include <unity.h>

// Golden output of the audio path. Each scenario drives one sound family
// with a fixed pseudo-random register script and hashes every DAC word
// Audio::transmit() hands to I2S. The hashes were recorded from the code
// before the audio refactoring (2026-10-15); the refactored code must
// reproduce them bit for bit. Needs the ROM headers romconv generates,
// like the device build. Arduino.h, FastLED.h, esp_attr.h, pgmspace.h and
// driver/i2s.h in this directory are host stubs.

// pokey, discrete and the Atari sound boards build as deps_*.cpp (own
// translation units: their file-static names collide)
#include "../../src/emulation/audio.cpp"
#include "../../src/machines/zaxxon/zaxxon_sound.h"  // .cpp: deps_zaxxon_sound.cpp
#include "../../src/emulation/ay8910.h"              // .cpp: deps_ay8910.cpp
#include "../../src/emulation/ym2203.h"              // .cpp: deps_ym2203.cpp
#include "../../src/emulation/ym2203fm.h"            // .cpp: deps_ym2203fm.cpp
#include "../../src/emulation/sn76489.h"             // .cpp: deps_sn76489.cpp
#include "../../src/emulation/namco_wsg.h"           // .cpp: deps_namco_wsg.cpp
#include "../../src/emulation/pokey.h"               // .cpp: deps_pokey.cpp
#include "../../src/machines/galaxian/galaxian_sound.h"            // .cpp: deps_galaxian_sound.cpp
#include "../../src/machines/bagman/bagman_sound.h"                // .cpp: deps_bagman_sound.cpp
#include "../../src/machines/spaceinvaders/spaceinvaders_sound.h"  // .cpp: deps_spaceinvaders_sound.cpp
#include "../../src/machines/phoenix/phoenix_sound.h"              // .cpp: deps_phoenix_sound.cpp
#include "../../src/machines/vanguard/snk6502_sound.h"             // .cpp: deps_snk6502_sound.cpp

// machineBase::reset() is in the vtable; the harness never resets CPUs
extern "C" {
void ResetZ80(Z80 *) {}
void Reset6502(M6502 *) {}
}

static const int FRAMES = 300;
static const int CHUNKS_PER_FRAME = 6;  // Audio::transmit() per 60 Hz frame
static const uint32_t FNV_OFFSET = 2166136261u;
static const uint32_t FNV_PRIME = 16777619u;
static const uint32_t SCRIPT_SEED = 12345;
static const uint32_t TABLE_SEED = 777;
static const int SAMPLE_BYTES = 256;
static const uint32_t SAMPLE_NIBBLES = 400;  // < 2 * (SAMPLE_BYTES - 2)

// recorded 2026-10-15 from the pre-refactoring Audio
static const uint32_t GOLDEN_AY_FROGGER = 0x25b45de4;
static const uint32_t GOLDEN_AY_BOMBJACK = 0x432754b5;
static const uint32_t GOLDEN_AY_GYRUSS = 0x4fbc7d26;
static const uint32_t GOLDEN_AY_MOTORACE = 0xa43fe198;
static const uint32_t GOLDEN_AY_FM_1943 = 0x23acec14;
static const uint32_t GOLDEN_SN_MRDO = 0x54dd45b6;
static const uint32_t GOLDEN_SN_TP84 = 0xfddd608a;
static const uint32_t GOLDEN_SN_REGULUS = 0x412a2394;
static const uint32_t GOLDEN_SN_GBERET = 0x1166d103;
static const uint32_t GOLDEN_NAMCO_PACMAN = 0x3807797a;
static const uint32_t GOLDEN_NAMCO15XX_MAPPY = 0xdd0ba59e;
static const uint32_t GOLDEN_POKEY_CENTIPEDE = 0xb61a23c4;
static const uint32_t GOLDEN_POKEY_MILLIPEDE = 0xc69e12af;
static const uint32_t GOLDEN_POKEY_CCASTLES = 0xd8516f7f;
static const uint32_t GOLDEN_POKEY_STARWARS = 0x165339d4;
static const uint32_t GOLDEN_GALAXIAN = 0x2b3fde93;
static const uint32_t GOLDEN_PHOENIX = 0xd293ae75;
static const uint32_t GOLDEN_BAGMAN = 0x6bba1810;
static const uint32_t GOLDEN_VANGUARD = 0xca6798b9;
static const uint32_t GOLDEN_FANTASY = 0x491cbd0b;
static const uint32_t GOLDEN_ZAXXON = 0x12639c24;

// --- I2S capture -----------------------------------------------------------

static uint32_t hash;
static int writes_left;
static bool capturing;

esp_err_t i2s_write(i2s_port_t, const void *src, size_t size, size_t *out, uint32_t ticks) {
  // transmit() polls with ticks 0: accept one buffer per call, then "full"
  if (ticks == 0 && writes_left-- <= 0) {
    *out = 0;
    return 0;
  }
  *out = size;
  if (!capturing) {
    return 0;
  }
  const uint8_t *p = static_cast<const uint8_t *>(src);
  for (size_t i = 0; i < size; i++) {
    hash = (hash ^ p[i]) * FNV_PRIME;
  }
  return 0;
}

esp_err_t i2s_set_sample_rates(i2s_port_t, uint32_t) { return 0; }

// --- deterministic script --------------------------------------------------

static uint32_t rng;
static uint8_t rnd(void) {
  rng = rng * 1664525u + 1013904223u;
  return rng >> 24;
}

static signed char fake_waves[8][32];
static uint8_t fake_sample[SAMPLE_BYTES];

static void fillTables(void) {
  rng = TABLE_SEED;
  for (int w = 0; w < 8; w++) {
    for (int i = 0; i < 32; i++) {
      fake_waves[w][i] = (signed char)(rnd() & 0x0f) - 8;
    }
  }
  for (int i = 0; i < SAMPLE_BYTES; i++) {
    fake_sample[i] = rnd();
  }
}

// FM / SID stand-in: +-300 sawtooth
static void saw(uint32_t &phase, int16_t *out, int n) {
  for (int i = 0; i < n; i++) {
    out[i] = (int16_t)((phase * 37) % 600) - 300;
    phase++;
  }
}

// speech stand-in: full int16 range as the TMS5220 renders it
static void speechSaw(uint32_t &phase, int16_t *out, int n) {
  for (int i = 0; i < n; i++) {
    out[i] = (int16_t)((int32_t)((phase * 911) % 40000) - 20000);
    phase++;
  }
}

// --- scenario runner -------------------------------------------------------

typedef void (*Poke)(machineBase *m, int frame);

static uint32_t run(const char *name, machineBase *m, Poke poke, bool mute = false) {
  Audio *audio = new Audio();
  audio->init();
  m->init(nullptr, nullptr, nullptr, nullptr);
  audio->start(m);
  audio->mute(mute ? 1 : 0);
  rng = SCRIPT_SEED;
  hash = FNV_OFFSET;
  for (int f = 0; f < FRAMES; f++) {
    if (poke) {
      poke(m, f);
    }
    for (int c = 0; c < CHUNKS_PER_FRAME; c++) {
      writes_left = 1;
      capturing = true;
      audio->transmit();
      capturing = false;
    }
  }
  audio->stop();
  delete audio;
  printf("golden %s 0x%08x\n", name, (unsigned)hash);
  return hash;
}

// --- register scripts ------------------------------------------------------

static void pokeRegs(uint8_t *regs, int span, const uint8_t *mask, int mask_len, int count) {
  for (int k = 0; k < count; k++) {
    const int r = rnd() % span;
    regs[r] = rnd() & mask[r % mask_len];
  }
}

static const uint8_t AY_MASK[16] = {0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0x3f,
                                    0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x1f, 0x00, 0x00};
static int ay_chips;
static void pokeAy(machineBase *m, int) { pokeRegs(m->soundregs, 16 * ay_chips, AY_MASK, 16, 6); }

struct SnView {
  int *period;
  int *volume;
  int *min_volume;
  int *hold;
};
static SnView sn_view[3];
static int sn_chips;
static void pokeSn(machineBase *, int) {
  for (int k = 0; k < 4; k++) {
    SnView &s = sn_view[rnd() % sn_chips];
    const int ch = rnd() & 3;
    const int what = rnd() % 3;
    if (what == 0) {
      const int hi = rnd() & 0x3f;
      const int lo = rnd() & 0x0f;
      s.period[ch] = ch == 3 ? (lo & 7) : (hi << 4 | lo);
    } else if (what == 1) {
      s.volume[ch] = rnd() & 15;
    } else {
      s.hold[ch] = rnd() % 7;
      s.min_volume[ch] = rnd() & 15;
    }
  }
}

static const uint8_t NIBBLE_MASK[1] = {0x0f};
static const uint8_t BYTE_MASK[1] = {0xff};
static void pokeNamco(machineBase *m, int) { pokeRegs(m->soundregs, 0x20, NIBBLE_MASK, 1, 6); }

static bool namco_enabled;
static void pokeNamco15xx(machineBase *m, int frame) {
  pokeRegs(m->soundregs, 0x40, BYTE_MASK, 1, 8);
  namco_enabled = (frame / 50) % 2 == 0;
}

static int pokey_chips;
static void pokePokey(machineBase *m, int) { pokeRegs(m->soundregs, 10 * pokey_chips, BYTE_MASK, 1, 6); }

static const uint8_t GALAXIAN_MASK[16] = {0xff, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1};
static void pokeGalaxian(machineBase *m, int) { pokeRegs(m->soundregs, 16, GALAXIAN_MASK, 16, 3); }

static void pokePhoenix(machineBase *m, int) { pokeRegs(m->soundregs, 2, BYTE_MASK, 1, 1); }

static const uint8_t BAGMAN_NOTES[9] = {0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0xe, 0xf, 0xb};
static void pokeBagman(machineBase *m, int frame) {
  if (frame % 20 != 0) {
    return;
  }
  m->soundregs[0] = rnd() % 60;
  m->soundregs[1] = 0;
  m->soundregs[2] = BAGMAN_NOTES[rnd() % 9];
  m->soundregs[3] = rnd() & 3;
}

static bool snk_muted[3];
static const uint8_t SNK_PORTS[4] = {0, 1, 2, 10};
static void pokeSnk(machineBase *m, int) {
  uint8_t *r = m->soundregs;
  const int port = rnd() % 4;
  r[SNK_PORTS[port]] = rnd();
  if (rnd() < 40) {
    r[5]++;  // port 0 write strobe
  }
  if (rnd() < 40) {
    r[6]++;
  }
  if (rnd() < 8) {
    r[8]++;  // music restarts
  }
  if (rnd() < 8) {
    r[9]++;
  }
  if (rnd() < 8) {
    r[11]++;
  }
  if (rnd() < 6) {
    const bool stop = rnd() % 4 == 0;
    const uint8_t phrase = rnd() & 15;
    r[3] = stop ? 0xff : phrase;
    r[4]++;
  }
  if (rnd() < 10) {
    const int ch = rnd() % 3;
    snk_muted[ch] = !snk_muted[ch];
  }
}

static void pokeZaxxon(machineBase *m, int) {
  if (rnd() < 60) {
    m->soundregs[0] = rnd();
  }
  if (rnd() < 50) {
    m->soundregs[ZAXXON_TRIG_BASE + 4 + rnd() % 6]++;
  }
}

// --- machines --------------------------------------------------------------

static const uint8_t AY_REGS[Ay8910::REG_COUNT] = {0x40, 0, 0x55, 0, 0x99, 1, 5, 0x00,
                                                   0x1f, 10, 15, 0x20, 0, 0x0e};
// --- tests -----------------------------------------------------------------

void setUp(void) {
  namco_enabled = true;
  snk_muted[0] = snk_muted[1] = snk_muted[2] = true;
}
void tearDown(void) {}

// as frogger: 1 chip, 9, 11
class AyFrogger : public machineBase {
public:
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
  Ay8910 ay[1] = {Ay8910(9, 11)};
};

class AyBombjack : public machineBase {
public:
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
  Ay8910 ay[3] = {Ay8910(8, 4), Ay8910(8, 4), Ay8910(8, 4)};
};

class AyGyruss : public machineBase {
public:
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
  Ay8910 ay[5] = {Ay8910(9, 3, Ay8910::NO_PERIOD0_ENV), Ay8910(9, 3, Ay8910::NO_PERIOD0_ENV),
                  Ay8910(9, 3, Ay8910::NO_PERIOD0_ENV), Ay8910(9, 3, Ay8910::NO_PERIOD0_ENV),
                  Ay8910(9, 3, Ay8910::NO_PERIOD0_ENV)};
};

class AyMotorace : public machineBase {
public:
  void renderAudio(int16_t *out, int n) override { Ay8910::renderBank(ay, soundregs, out, n); }
  Ay8910 ay[2] = {Ay8910(5, 5, Ay8910::NO_NOISE), Ay8910(5, 5, Ay8910::NO_NOISE)};
};

// as 1943: YM2203 FM part plus SSG, clipped
class AyFm1943 : public machineBase {
public:
  void renderAudio(int16_t *out, int n) override {
    int16_t ssg[AUDIO_CHUNK];
    saw(phase, out, n);
    Ay8910::renderBank(ay, soundregs, ssg, n);
    audioAdd(out, ssg, n);
    audioClampAll(out, n, AUDIO_MAX);
  }
  Ay8910 ay[2] = {Ay8910(4, 5), Ay8910(4, 5)};
  uint32_t phase = 0;
};

static void test_ay_frogger(void) {
  AyFrogger m;
  ay_chips = 1;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_AY_FROGGER, run("ay_frogger", &m, pokeAy));
}

static void test_ay_bombjack(void) {
  AyBombjack m;
  ay_chips = 3;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_AY_BOMBJACK, run("ay_bombjack", &m, pokeAy));
}

static void test_ay_gyruss(void) {
  AyGyruss m;
  ay_chips = 5;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_AY_GYRUSS, run("ay_gyruss", &m, pokeAy));
}

static void test_ay_motorace(void) {
  AyMotorace m;
  ay_chips = 2;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_AY_MOTORACE, run("ay_motorace", &m, pokeAy));
}

static void test_ay_fm_1943(void) {
  AyFm1943 m;
  ay_chips = 2;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_AY_FM_1943, run("ay_fm_1943", &m, pokeAy));
}

template <int N>
class SnFake : public machineBase {
public:
  explicit SnFake(const Sn76489 (&init)[N]) {
    for (int c = 0; c < N; c++) {
      sn[c] = init[c];
      sn_view[c] = {sn[c].period, sn[c].volume, sn[c].min_volume, sn[c].hold};
    }
  }
  void renderAudio(int16_t *out, int n) override { Sn76489::renderBank(sn, out, n); }
  Sn76489 sn[N];  // default-constructed, replaced in the constructor
};

static void test_sn_mrdo(void) {
  static const Sn76489 chips[2] = {Sn76489(11), Sn76489(11)};
  SnFake<2> m(chips);
  sn_chips = 2;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_SN_MRDO, run("sn_mrdo", &m, pokeSn));
}

static void test_sn_tp84(void) {
  static const Sn76489 chips[3] = {Sn76489(5), Sn76489(5), Sn76489(5)};
  SnFake<3> m(chips);
  sn_chips = 3;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_SN_TP84, run("sn_tp84", &m, pokeSn));
}

static void test_sn_regulus(void) {
  static const Sn76489 chips[2] = {Sn76489(5), Sn76489(11)};
  SnFake<2> m(chips);
  sn_chips = 2;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_SN_REGULUS, run("sn_regulus", &m, pokeSn));
}

static void test_sn_gberet(void) {
  static const Sn76489 chips[1] = {Sn76489(4)};
  SnFake<1> m(chips);
  sn_chips = 1;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_SN_GBERET, run("sn_gberet", &m, pokeSn));
}

class NamcoFake : public machineBase {
public:
  void renderAudio(int16_t *out, int n) override { wsg.render(soundregs, fake_waves, out, n); }
  NamcoWsg wsg;
};

class Namco15xxFake : public machineBase {
public:
  void renderAudio(int16_t *out, int n) override {
    wsg.render(soundregs, fake_waves, namco_enabled, out, n);
    audioShiftRight(out, n, 1);
  }
  Namco15xx wsg;
};

static void test_namco_pacman(void) {
  NamcoFake m;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_NAMCO_PACMAN, run("namco_pacman", &m, pokeNamco));
}

static void test_namco15xx_mappy(void) {
  Namco15xxFake m;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_NAMCO15XX_MAPPY, run("namco15xx_mappy", &m, pokeNamco15xx));
}

template <int N>
class PokeyFake : public machineBase {
public:
  explicit PokeyFake(int32_t clocks) {
    for (int c = 0; c < N; c++) {
      pokey[c].reset(clocks);
    }
  }
  void renderAudio(int16_t *out, int n) override {
    Pokey::renderBank(pokey, soundregs, out, n);
    audioClampAll(out, n, AUDIO_MAX);
  }
  Pokey pokey[N];
};

// as starwars: four POKEYs at half level plus TMS5220 speech
class PokeyStarwars : public machineBase {
public:
  PokeyStarwars() {
    for (int c = 0; c < 4; c++) {
      pokey[c].reset(Pokey::CLOCKS_1512KHZ);
    }
  }
  void renderAudio(int16_t *out, int n) override {
    int16_t voice[AUDIO_CHUNK];
    Pokey::renderBank(pokey, soundregs, out, n);
    speechSaw(phase, voice, n);
    for (int i = 0; i < n; i++) {
      out[i] = audioClamp((out[i] >> 1) + (voice[i] >> 6), AUDIO_MAX);
    }
  }
  Pokey pokey[4];
  uint32_t phase = 0;
};

static void test_pokey_centipede(void) {
  PokeyFake<1> m(Pokey::CLOCKS_1512KHZ);
  pokey_chips = 1;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_POKEY_CENTIPEDE, run("pokey_centipede", &m, pokePokey));
}
static void test_pokey_millipede(void) {
  PokeyFake<2> m(Pokey::CLOCKS_1512KHZ);
  pokey_chips = 2;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_POKEY_MILLIPEDE, run("pokey_millipede", &m, pokePokey));
}

static void test_pokey_ccastles(void) {
  PokeyFake<2> m(Pokey::CLOCKS_1250KHZ);
  pokey_chips = 2;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_POKEY_CCASTLES, run("pokey_ccastles", &m, pokePokey));
}

static void test_pokey_starwars(void) {
  PokeyStarwars m;
  pokey_chips = 4;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_POKEY_STARWARS, run("pokey_starwars", &m, pokePokey));
}

class PhoenixFake : public machineBase {
public:
  void renderAudio(int16_t *out, int n) override { sound.render(soundregs, out, n); }
  PhoenixSound sound;
};

static void test_phoenix(void) {
  PhoenixFake m;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_PHOENIX, run("phoenix", &m, pokePhoenix));
}

class GalaxianFake : public machineBase {
public:
  void renderAudio(int16_t *out, int n) override { sound.render(soundregs, out, n); }
  GalaxianSound sound;
};

class BagmanFake : public machineBase {
public:
  void renderAudio(int16_t *out, int n) override { sound.render(soundregs, out, n); }
  BagmanSound sound;
};

static void test_galaxian(void) {
  GalaxianFake m;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_GALAXIAN, run("galaxian", &m, pokeGalaxian));
}

static void test_bagman(void) {
  BagmanFake m;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_BAGMAN, run("bagman", &m, pokeBagman));
}

class SnkFake : public machineBase, private Snk6502Sound::Host {
public:
  explicit SnkFake(Snk6502Sound::Board board) : sound(*this, board) {}
  void init(Input *i, unsigned short *f, sprite_S *s, unsigned char *m) override {
    machineBase::init(i, f, s, m);
    sound.reset(soundregs);
  }
  void renderAudio(int16_t *out, int n) override { sound.render(soundregs, out, n); }

private:
  uint8_t soundRom(uint16_t a) override { return (a & 63) == 63 ? 0xff : (uint8_t)(a * 37 + 11); }
  bool musicMuted(int ch) override { return snk_muted[ch]; }
  void musicEnded() override { snk_muted[0] = true; }
  const signed char *sample(uint8_t) override { return (const signed char *)fake_sample; }
  uint32_t sampleLength(uint8_t) override { return SAMPLE_NIBBLES; }
  uint8_t sampleDivider(uint8_t i) override { return i < 2 ? 3 : 4; }

  Snk6502Sound sound;
};

static void test_vanguard(void) {
  SnkFake m(Snk6502Sound::VANGUARD);
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_VANGUARD, run("vanguard", &m, pokeSnk));
}

static void test_fantasy(void) {
  SnkFake m(Snk6502Sound::FANTASY);
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_FANTASY, run("fantasy", &m, pokeSnk));
}

class ZaxxonFake : public machineBase {
public:
  void renderAudio(int16_t *out, int n) override { sound.render(soundregs, out, n); }
  ZaxxonSound sound;
};

static void test_zaxxon(void) {
  ZaxxonFake m;
  TEST_ASSERT_EQUAL_HEX32(GOLDEN_ZAXXON, run("zaxxon", &m, pokeZaxxon));
}

// hash of FRAMES * CHUNKS_PER_FRAME buffers of idle level; 64 DAC words
// per buffer (SND_DIFF, which doubles them, is not set: config_local.h
// selects SND_RIGHT_CHANNEL)
static const int DAC_WORDS = 64;
static uint32_t silenceHash(void) {
  uint16_t words[DAC_WORDS];
  for (int i = 0; i < DAC_WORDS; i++) {
    words[i] = 0x8000;  // Audio::SILENCE
  }
  uint32_t h = FNV_OFFSET;
  for (int c = 0; c < FRAMES * CHUNKS_PER_FRAME; c++) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(words);
    for (size_t i = 0; i < sizeof(words); i++) {
      h = (h ^ p[i]) * FNV_PRIME;
    }
  }
  return h;
}

// menu: a machine without renderAudio() override plays silence
static void test_menu_silent(void) {
  machineBase m;
  TEST_ASSERT_EQUAL_HEX32(silenceHash(), run("menu", &m, nullptr));
}

// mute is applied by the sink for every machine
static void test_mute(void) {
  AyFrogger m;
  ay_chips = 1;
  TEST_ASSERT_EQUAL_HEX32(silenceHash(), run("mute", &m, pokeAy, true));
}

// Builds T twice in memory pre-filled with different garbage and compares
// ROUNDS rendered chunks: a member without an initializer (machines are
// heap-allocated) shows up as a difference.
static const int ROUNDS = 20;
template <class T, class Build, class Render>
static void assertInitIndependent(Build build, Render render) {
  alignas(T) static uint8_t mem[2][sizeof(T)];
  T *obj[2];
  int16_t out[2][AUDIO_CHUNK];
  for (int k = 0; k < 2; k++) {
    memset(mem[k], k ? 0x5a : 0xa5, sizeof(T));
    obj[k] = build(mem[k]);
  }
  for (int r = 0; r < ROUNDS; r++) {
    for (int k = 0; k < 2; k++) {
      render(*obj[k], out[k]);
    }
    TEST_ASSERT_EQUAL_INT16_ARRAY(out[0], out[1], AUDIO_CHUNK);
  }
  obj[0]->~T();
  obj[1]->~T();
}

// port A 0x03: bits 2-7 low, every loop channel starts; counters 1..6
// differ from trig_seen 0, so channels 4-9 start too
static const uint8_t ZAXXON_INIT_REGS[16] = {0x03, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6};
static void test_zaxxon_init(void) {
  assertInitIndependent<ZaxxonSound>(
      [](void *p) { return new (p) ZaxxonSound(); },
      [](ZaxxonSound &s, int16_t *out) { s.render(ZAXXON_INIT_REGS, out, AUDIO_CHUNK); });
}

static void test_ay_init(void) {
  assertInitIndependent<Ay8910>(
      [](void *p) { return new (p) Ay8910(8, 7); },
      [](Ay8910 &ay, int16_t *out) { ay.render(AY_REGS, out, AUDIO_CHUNK); });
}

// a YM2203 driven through its port renders like an Ay8910 given the same
// registers; data to FM registers ($10 and up) reaches the FM part only
static void test_ym2203_port(void) {
  static Ym2203Fm fm;  // large: keep off the stack
  fm.reset();
  Ym2203 ym(4, 5, &fm);
  Ay8910 ay(4, 5);
  uint8_t regs[Ay8910::REG_COUNT] = {};
  rng = SCRIPT_SEED;
  for (int chunk = 0; chunk < 200; chunk++) {
    for (int k = 0; k < 6; k++) {
      const uint8_t reg = rnd() % 0x30;  // SSG and FM registers
      const uint8_t value = rnd();
      ym.writeAddress(reg);
      ym.writeData(value);
      if (reg < Ay8910::REG_COUNT) {
        regs[reg] = value;
      }
    }
    int16_t a[AUDIO_CHUNK], b[AUDIO_CHUNK];
    ym.renderSsg(a, AUDIO_CHUNK);
    ay.render(regs, b, AUDIO_CHUNK);
    TEST_ASSERT_EQUAL_INT16_ARRAY(b, a, AUDIO_CHUNK);
  }
}

// without an FM part, FM writes are dropped (commando, gng)
static void test_ym2203_no_fm(void) {
  Ym2203 ym(4, 5);
  ym.writeAddress(0x28);  // FM key on
  ym.writeData(0xf0);
  int16_t out[AUDIO_CHUNK];
  ym.renderSsg(out, AUDIO_CHUNK);
  for (int i = 0; i < AUDIO_CHUNK; i++) {
    TEST_ASSERT_EQUAL_INT16(0, out[i]);
  }
}

static void test_ym2203_init(void) {
  assertInitIndependent<Ym2203>(
      [](void *p) {
        Ym2203 *y = new (p) Ym2203(4, 5);
        for (uint8_t r = 0; r < Ay8910::REG_COUNT; r++) {
          y->writeAddress(r);
          y->writeData(AY_REGS[r]);
        }
        return y;
      },
      [](Ym2203 &y, int16_t *out) { y.renderSsg(out, AUDIO_CHUNK); });
}

static void test_sn_init(void) {
  assertInitIndependent<Sn76489>(
      [](void *p) {
        Sn76489 *s = new (p) Sn76489(11);
        s->period[0] = 100;
        s->volume[0] = 3;
        s->period[3] = 2;
        s->volume[3] = 5;
        return s;
      },
      [](Sn76489 &s, int16_t *out) { s.render(out, AUDIO_CHUNK); });
}

static uint8_t namco_regs[Namco15xx::REG_COUNT];
static void test_namco_init(void) {
  for (int i = 0; i < Namco15xx::REG_COUNT; i++) {
    namco_regs[i] = (uint8_t)(i * 7 + 3);
  }
  assertInitIndependent<NamcoWsg>(
      [](void *p) { return new (p) NamcoWsg(); },
      [](NamcoWsg &w, int16_t *out) { w.render(namco_regs, fake_waves, out, AUDIO_CHUNK); });
  assertInitIndependent<Namco15xx>(
      [](void *p) { return new (p) Namco15xx(); },
      [](Namco15xx &w, int16_t *out) { w.render(namco_regs, fake_waves, true, out, AUDIO_CHUNK); });
}

static uint8_t pokey_regs[Pokey::REG_COUNT] = {0x40, 0xa8, 0x60, 0xa6, 0, 0, 0, 0, 0, 0x03};
static void test_pokey_init(void) {
  assertInitIndependent<Pokey>(
      [](void *p) {
        Pokey *k = new (p) Pokey();
        k->reset(Pokey::CLOCKS_1512KHZ);
        return k;
      },
      [](Pokey &k, int16_t *out) { k.render(pokey_regs, out, AUDIO_CHUNK); });
}

static const uint8_t GALAXIAN_REGS[16] = {0x80, 1, 1, 1, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 0};
static uint8_t board_regs[16];
static void test_board_init(void) {
  assertInitIndependent<GalaxianSound>(
      [](void *p) { return new (p) GalaxianSound(); },
      [](GalaxianSound &s, int16_t *out) { s.render(GALAXIAN_REGS, out, AUDIO_CHUNK); });
  assertInitIndependent<BagmanSound>(
      [](void *p) {
        board_regs[0] = 50;
        board_regs[2] = 0x5;
        return new (p) BagmanSound();
      },
      [](BagmanSound &s, int16_t *out) {
        board_regs[0] = 50;  // both instances see the same duration
        s.render(board_regs, out, AUDIO_CHUNK);
      });
  assertInitIndependent<SpaceInvadersSound>(
      [](void *p) { return new (p) SpaceInvadersSound(fake_sample, SAMPLE_BYTES - 2, fake_sample, SAMPLE_BYTES - 2); },
      [](SpaceInvadersSound &s, int16_t *out) {
        board_regs[0] = 0x0f;  // UFO, shot, explosion, invader die
        board_regs[1] = 0x11;  // fleet 1, UFO hit
        board_regs[2] = 0;     // no coin: render() clears it, both instances must see the same
        s.render(board_regs, out, AUDIO_CHUNK);
      });
}

static const uint8_t PHOENIX_REGS[2] = {0xc5, 0x3a};
static void test_phoenix_init(void) {
  assertInitIndependent<PhoenixSound>(
      [](void *p) { return new (p) PhoenixSound(); },
      [](PhoenixSound &s, int16_t *out) { s.render(PHOENIX_REGS, out, AUDIO_CHUNK); });
}

// sign changes in n rendered samples: square-wave toggles
static int toggles(Sn76489 &sn, int chunks) {
  int count = 0;
  int16_t last = 0;
  for (int c = 0; c < chunks; c++) {
    int16_t out[AUDIO_CHUNK];
    sn.render(out, AUDIO_CHUNK);
    for (int i = 0; i < AUDIO_CHUNK; i++) {
      if (out[i] != 0 && last != 0 && (out[i] > 0) != (last > 0)) {
        count++;
      }
      if (out[i] != 0) {
        last = out[i];
      }
    }
  }
  return count;
}

static int16_t peak(Sn76489 &sn) {
  int16_t out[AUDIO_CHUNK];
  sn.render(out, AUDIO_CHUNK);
  int16_t p = 0;
  for (int i = 0; i < AUDIO_CHUNK; i++) {
    p = out[i] > p ? out[i] : (-out[i] > p ? -out[i] : p);
  }
  return p;
}

static const uint8_t SN_LATCH = 0x80;
static uint8_t snLatch(int ch, bool volume, uint8_t low) { return SN_LATCH | ch << 5 | (volume ? 0x10 : 0) | low; }

// tone: latch carries bits 0-3, data byte bits 4-9; period 110 at inc 11
// toggles every 10 samples
static void test_sn_write_tone(void) {
  Sn76489 sn(Sn76489::INC_4MHZ);
  sn.write(snLatch(0, false, 110 & 0x0f));
  sn.write(110 >> 4);
  sn.write(snLatch(0, true, 0));  // volume 0 = loudest
  TEST_ASSERT_INT_WITHIN(2, 10 * AUDIO_CHUNK / 10, toggles(sn, 10));  // 640 samples / 10
}

// volume: latch sets it; a data byte after a volume latch sets it too (MAME)
static void test_sn_write_volume(void) {
  Sn76489 sn(Sn76489::INC_4MHZ);
  sn.write(snLatch(1, false, 0x0f));
  sn.write(0x02);
  sn.write(snLatch(1, true, 0));
  TEST_ASSERT_EQUAL_INT16(15 * 6, peak(sn));
  sn.write(10);  // data byte: volume 10
  TEST_ASSERT_EQUAL_INT16(5 * 6, peak(sn));
  sn.write(snLatch(1, true, 15));
  TEST_ASSERT_EQUAL_INT16(0, peak(sn));
}

// noise rate 3 follows tone 2: a lower tone 2 slows the noise down
static void test_sn_write_noise_follows_tone2(void) {
  Sn76489 fast(Sn76489::INC_4MHZ), slow(Sn76489::INC_4MHZ);
  Sn76489 *chips[2] = {&fast, &slow};
  const int periods[2] = {40, 400};
  for (int k = 0; k < 2; k++) {
    chips[k]->write(snLatch(3, false, 3));  // noise: follow tone 2
    chips[k]->write(snLatch(2, false, periods[k] & 0x0f));
    chips[k]->write(periods[k] >> 4);
    chips[k]->write(snLatch(3, true, 0));
  }
  TEST_ASSERT_TRUE(toggles(fast, 20) > 4 * toggles(slow, 20));
}

// noise rates 0..2 halve the shift rate per step
static void test_sn_write_noise_rates(void) {
  int t[3];
  for (int rate = 0; rate < 3; rate++) {
    Sn76489 sn(Sn76489::INC_4MHZ);
    sn.write(snLatch(3, false, rate));
    sn.write(snLatch(3, true, 0));
    t[rate] = toggles(sn, 100);
  }
  TEST_ASSERT_TRUE(t[0] > t[1] && t[1] > t[2]);
}

// hold: a note switched on and off between two renders still sounds,
// at its loudest volume, for HOLD_RENDERS renders; without the flag it is lost
static void test_sn_write_hold(void) {
  Sn76489 held(Sn76489::INC_4MHZ, Sn76489::HOLD_SHORT_NOTES), plain(Sn76489::INC_4MHZ);
  Sn76489 *chips[2] = {&held, &plain};
  for (int k = 0; k < 2; k++) {
    chips[k]->write(snLatch(0, false, 0x0f));
    chips[k]->write(0x02);
    chips[k]->write(snLatch(0, true, 0));
    chips[k]->write(snLatch(0, true, 8));
    chips[k]->write(snLatch(0, true, 15));
  }
  TEST_ASSERT_EQUAL_INT16(15 * 6, peak(held));
  TEST_ASSERT_EQUAL_INT16(0, peak(plain));
}

int main(int, char **) {
  fillTables();
  UNITY_BEGIN();
  RUN_TEST(test_ay_frogger);
  RUN_TEST(test_ay_bombjack);
  RUN_TEST(test_ay_gyruss);
  RUN_TEST(test_ay_motorace);
  RUN_TEST(test_ay_fm_1943);
  RUN_TEST(test_sn_mrdo);
  RUN_TEST(test_sn_tp84);
  RUN_TEST(test_sn_regulus);
  RUN_TEST(test_sn_gberet);
  RUN_TEST(test_namco_pacman);
  RUN_TEST(test_namco15xx_mappy);
  RUN_TEST(test_pokey_centipede);
  RUN_TEST(test_pokey_millipede);
  RUN_TEST(test_pokey_ccastles);
  RUN_TEST(test_pokey_starwars);
  RUN_TEST(test_galaxian);
  RUN_TEST(test_phoenix);
  RUN_TEST(test_bagman);
  RUN_TEST(test_vanguard);
  RUN_TEST(test_fantasy);
  RUN_TEST(test_zaxxon);
  RUN_TEST(test_menu_silent);
  RUN_TEST(test_mute);
  RUN_TEST(test_zaxxon_init);
  RUN_TEST(test_ay_init);
  RUN_TEST(test_ym2203_port);
  RUN_TEST(test_ym2203_no_fm);
  RUN_TEST(test_ym2203_init);
  RUN_TEST(test_sn_init);
  RUN_TEST(test_sn_write_tone);
  RUN_TEST(test_sn_write_volume);
  RUN_TEST(test_sn_write_noise_follows_tone2);
  RUN_TEST(test_sn_write_noise_rates);
  RUN_TEST(test_sn_write_hold);
  RUN_TEST(test_namco_init);
  RUN_TEST(test_pokey_init);
  RUN_TEST(test_board_init);
  RUN_TEST(test_phoenix_init);
  return UNITY_END();
}
