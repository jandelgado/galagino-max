#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "../../src/cpus/m6502/m6502.c"
#include "../../src/emulation/sid.cpp"
#include "../../src/emulation/sidplayer.cpp"

// synthetic PSID, load $1000
static const uint16_t LOAD = 0x1000;
static const uint8_t PLAY_COUNT = 0x20, SONG_INDEX = 0x21; // ram offsets
static const uint8_t TUNE_CODE[] = {
  0x8d, 0x21, 0x10, // $1000 init: STA $1021   song index from A
  0xa9, 0x0f,       //            LDA #$0F
  0x8d, 0x18, 0xd4, //            STA $D418   volume
  0x60,             //            RTS
  0xee, 0x20, 0x10, // $1009 play: INC $1020   play counter
  0x8d, 0x00, 0x20, //            STA $2000   outside the RAM window
  0x60,             //            RTS
};
static const uint16_t PLAY = 0x1009;
static const uint8_t LOOP_CODE[] = {0x4c, 0x00, 0x10}; // JMP $1000

static uint8_t file[0x200];
static uint8_t ram[0x40];
static int16_t buf[24000];
static SidPlayer player;
static PsidInfo info;

static void put16(uint8_t *p, uint16_t v) {
  p[0] = v >> 8;
  p[1] = v & 0xff;
}

static uint32_t buildPsid(const uint8_t *code, uint16_t len, uint16_t init, uint16_t play) {
  memset(file, 0, sizeof(file));
  memcpy(file, "PSID", 4);
  put16(file + 0x04, 2);
  put16(file + 0x06, 0x7c);
  put16(file + 0x08, 0); // load address from the data
  put16(file + 0x0a, init);
  put16(file + 0x0c, play);
  put16(file + 0x0e, 2); // songs
  put16(file + 0x10, 1); // start song
  file[0x7c] = LOAD & 0xff;
  file[0x7d] = LOAD >> 8;
  memcpy(file + 0x7e, code, len);
  return 0x7e + len;
}

static void loadSynthetic(void) {
  const uint32_t len = buildPsid(TUNE_CODE, sizeof(TUNE_CODE), LOAD, PLAY);
  TEST_ASSERT_TRUE(psidParse(file, len, &info));
  TEST_ASSERT_TRUE(player.load(info, ram, sizeof(ram)));
}

void setUp(void) { memset(ram, 0, sizeof(ram)); }
void tearDown(void) {}

static void test_psid_parse_synthetic(void) {
  const uint32_t len = buildPsid(TUNE_CODE, sizeof(TUNE_CODE), LOAD, PLAY);
  TEST_ASSERT_TRUE(psidParse(file, len, &info));
  TEST_ASSERT_EQUAL_HEX16(LOAD, info.load);
  TEST_ASSERT_EQUAL_HEX16(LOAD, info.init);
  TEST_ASSERT_EQUAL_HEX16(PLAY, info.play);
  TEST_ASSERT_EQUAL(2, info.songs);
  TEST_ASSERT_EQUAL(1, info.start_song);
  TEST_ASSERT_EQUAL(sizeof(TUNE_CODE), info.payload_len);
  TEST_ASSERT_EQUAL_HEX8(0x8d, info.payload[0]);
}

static void test_psid_rejects_bad_files(void) {
  uint32_t len = buildPsid(TUNE_CODE, sizeof(TUNE_CODE), LOAD, PLAY);
  TEST_ASSERT_FALSE(psidParse(file, 0x10, &info)); // truncated header

  memcpy(file, "RSID", 4); // needs a real C64 environment
  TEST_ASSERT_FALSE(psidParse(file, len, &info));

  len = buildPsid(TUNE_CODE, sizeof(TUNE_CODE), LOAD, 0); // IRQ installed player
  TEST_ASSERT_FALSE(psidParse(file, len, &info));

  len = buildPsid(TUNE_CODE, sizeof(TUNE_CODE), LOAD, PLAY);
  file[0x15] = 1; // speed: song 1 CIA timed
  TEST_ASSERT_FALSE(psidParse(file, len, &info));
}

// PSID spec: start song 0 or > songs means 1, init 0 means load
static void test_start_song_zero_defaults_to_1(void) {
  const uint32_t len = buildPsid(TUNE_CODE, sizeof(TUNE_CODE), LOAD, PLAY);
  put16(file + 0x10, 0);
  TEST_ASSERT_TRUE(psidParse(file, len, &info));
  TEST_ASSERT_EQUAL(1, info.start_song);
  put16(file + 0x10, 3); // songs = 2
  TEST_ASSERT_TRUE(psidParse(file, len, &info));
  TEST_ASSERT_EQUAL(1, info.start_song);
}

static void test_init_zero_uses_load(void) {
  const uint32_t len = buildPsid(TUNE_CODE, sizeof(TUNE_CODE), 0, PLAY);
  TEST_ASSERT_TRUE(psidParse(file, len, &info));
  TEST_ASSERT_EQUAL_HEX16(LOAD, info.init);
}

static void test_load_rejects_small_ram(void) {
  const uint32_t len = buildPsid(TUNE_CODE, sizeof(TUNE_CODE), LOAD, PLAY);
  TEST_ASSERT_TRUE(psidParse(file, len, &info));
  TEST_ASSERT_FALSE(player.load(info, ram, sizeof(TUNE_CODE) - 1));
}

static void test_init_gets_song_index(void) {
  loadSynthetic();
  player.start(2);
  TEST_ASSERT_EQUAL(1, ram[SONG_INDEX]);
}

static void test_play_at_50hz(void) {
  loadSynthetic();
  player.start(1);
  for (int i = 0; i < 24000; i += 64) {
    player.render(buf, 64); // audio buffer size
  }
  TEST_ASSERT_EQUAL(SidPlayer::PLAY_HZ, ram[PLAY_COUNT]);
}

static void test_strays_counted(void) {
  loadSynthetic();
  player.start(1);
  player.render(buf, 1); // one play call
  TEST_ASSERT_EQUAL(1, player.strays());
}

static void test_restart_reloads_payload(void) {
  loadSynthetic();
  player.start(1);
  player.render(buf, 24000);
  TEST_ASSERT_NOT_EQUAL(0, ram[PLAY_COUNT]);
  player.start(1);
  TEST_ASSERT_EQUAL(0, ram[PLAY_COUNT]);
}

static void test_runaway_play_stops(void) {
  const uint32_t len = buildPsid(LOOP_CODE, sizeof(LOOP_CODE), LOAD + 3, LOAD);
  file[0x7e + 3] = 0x60; // init at $1003: RTS
  TEST_ASSERT_TRUE(psidParse(file, len + 1, &info));
  TEST_ASSERT_TRUE(player.load(info, ram, sizeof(ram)));
  player.start(1);
  player.render(buf, 24000); // must return
  TEST_PASS();
}

// --- real tune, romszip/Commando.sid (user supplied, not in the repo) ---

static const uint16_t TUNE_RAM_SIZE = 0x1000;
static uint8_t tune_file[0x2000];
static uint8_t tune_ram[TUNE_RAM_SIZE];

static uint32_t loadCommando(void) {
  const char *paths[] = {"../romszip/Commando.sid", "romszip/Commando.sid"};
  for (const char *p : paths) {
    FILE *f = fopen(p, "rb");
    if (!f) {
      continue;
    }
    const uint32_t len = fread(tune_file, 1, sizeof(tune_file), f);
    fclose(f);
    return len;
  }
  return 0;
}

static void writeWav(const char *path, const int16_t *s, uint32_t n) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    return;
  }
  const uint32_t rate = Sid::SAMPLE_RATE, bytes = n * 2;
  const uint32_t hdr32[] = {36 + bytes, 16, 0x00010001, rate, rate * 2, 0x00100002};
  fwrite("RIFF", 1, 4, f);
  fwrite(&hdr32[0], 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&hdr32[1], 4, 5, f);
  fwrite("data", 1, 4, f);
  fwrite(&bytes, 4, 1, f);
  for (uint32_t i = 0; i < n; i++) {
    const int16_t v = s[i] * 64; // +-480 -> +-30720, like valueToBuffer()
    fwrite(&v, 2, 1, f);
  }
  fclose(f);
}

static void test_commando_header(void) {
  const uint32_t len = loadCommando();
  if (!len) {
    TEST_IGNORE_MESSAGE("romszip/Commando.sid missing");
  }
  TEST_ASSERT_TRUE(psidParse(tune_file, len, &info));
  TEST_ASSERT_EQUAL_HEX16(0x5000, info.load);
  TEST_ASSERT_EQUAL_HEX16(0x5fb2, info.init);
  TEST_ASSERT_EQUAL_HEX16(0x5012, info.play);
  TEST_ASSERT_EQUAL(19, info.songs);
  TEST_ASSERT_EQUAL(4039, info.payload_len);
}

// memory probe: every subtune, 30 s, no access outside $0000-$01FF,
// $5000-$5FFF and the SID
static void test_commando_stays_in_window(void) {
  const uint32_t len = loadCommando();
  if (!len) {
    TEST_IGNORE_MESSAGE("romszip/Commando.sid missing");
  }
  TEST_ASSERT_TRUE(psidParse(tune_file, len, &info));
  TEST_ASSERT_TRUE(player.load(info, tune_ram, TUNE_RAM_SIZE));
  for (uint8_t song = 1; song <= info.songs; song++) {
    player.start(song);
    for (int s = 0; s < 30; s++) {
      player.render(buf, 24000);
    }
    TEST_ASSERT_EQUAL_MESSAGE(0, player.strays(), "stray access, see plan Task 3 Step 5");
  }
}

// listening material: SID_WAV_DIR=/some/dir writes song_NN.wav, 60 s each
static void test_commando_wav_dump(void) {
  const char *dir = getenv("SID_WAV_DIR");
  const uint32_t len = loadCommando();
  if (!dir || !len) {
    TEST_IGNORE_MESSAGE("set SID_WAV_DIR and supply romszip/Commando.sid");
  }
  static int16_t song_buf[60 * 24000];
  TEST_ASSERT_TRUE(psidParse(tune_file, len, &info));
  TEST_ASSERT_TRUE(player.load(info, tune_ram, TUNE_RAM_SIZE));
  for (uint8_t song = 1; song <= info.songs; song++) {
    player.start(song);
    player.render(song_buf, 60 * 24000);
    char path[512];
    snprintf(path, sizeof(path), "%s/song_%02u.wav", dir, song);
    writeWav(path, song_buf, 60 * 24000);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_psid_parse_synthetic);
  RUN_TEST(test_psid_rejects_bad_files);
  RUN_TEST(test_start_song_zero_defaults_to_1);
  RUN_TEST(test_init_zero_uses_load);
  RUN_TEST(test_load_rejects_small_ram);
  RUN_TEST(test_init_gets_song_index);
  RUN_TEST(test_play_at_50hz);
  RUN_TEST(test_strays_counted);
  RUN_TEST(test_restart_reloads_payload);
  RUN_TEST(test_runaway_play_stops);
  RUN_TEST(test_commando_header);
  RUN_TEST(test_commando_stays_in_window);
  RUN_TEST(test_commando_wav_dump);
  return UNITY_END();
}
