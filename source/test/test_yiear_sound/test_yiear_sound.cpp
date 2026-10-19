#include <stdio.h>
#include <string.h>
#include <unity.h>

// Yie Ar Kung-Fu sound on the real ROMs: every VLM5030 phrase of 407_c09
// speaks and ends, and the attract mode drives both SN76489 and VLM5030
// through the machine class. ROM headers come from romconv (./convert.sh
// yiear); without them the tests are skipped.
#if __has_include("../../src/machines/yiear/yiear_rom.h")
#define HAVE_ROM 1
#define ENABLE_YIEAR
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/emulation/sn76489.cpp"
#include "../../src/emulation/vlm5030.cpp"
#define private public
#define protected public
#include "../../src/machines/yiear/yiear.cpp"
#undef private
#undef protected
#endif

void setUp(void) {}
void tearDown(void) {}

#ifdef HAVE_ROM
static Input input;
static unsigned short frame_buffer[240 * 8];
static sprite_S sprites[128];
static unsigned char memory[RAMSIZE];
static yiear *machine;

unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" void ResetZ80(Z80 *) {}
void Reset6502(M6502 *) {}
unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return machine->m6809_read_opcode(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }



static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new yiear();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

// phrase table entries 0-9 and 20-25 are used, 10-19 read 0xffff.
// Measured 2026-10-16: 1201..12881 samples, peaks 117..235 of 512.
static void test_vlm_phrases(void) {
  boot();
  static const uint8_t PHRASES[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 20, 21, 22, 23, 24, 25};
  Vlm5030 vlm;
  for (unsigned i = 0; i < sizeof(PHRASES); i++) {
    vlm.reset(machine->vlm_rom, 0x2000);
    vlm.data_w(PHRASES[i] * 2);
    vlm.st_w(true);
    vlm.st_w(false);
    int n = 0, peak = 0;
    while (vlm.bsy() && n < 3 * (int)VLM_HZ) {
      const int s = vlm.sample() >> 6;
      peak = abs(s) > peak ? abs(s) : peak;
      n++;
    }
    TEST_ASSERT_FALSE_MESSAGE(vlm.bsy(), "phrase never ends");
    TEST_ASSERT_TRUE(n > VLM_HZ / 10);
    TEST_ASSERT_TRUE(peak > 100 && peak < 511);  // audible, not clipping
  }
}

static void writeWav(const char *name, const int16_t *s, int n) {
  FILE *f = fopen(name, "wb");
  const uint32_t rate = AUDIO_HZ, bytes = n * 2, riff = 36 + bytes, fmt = 16, brate = rate * 2;
  const uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
  fwrite("RIFF", 4, 1, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 8, 1, f);
  fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
  fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 4, 1, f); fwrite(&bytes, 4, 1, f); fwrite(s, 2, n, f);
  fclose(f);
}

// Attract mode through the machine: the fight demo speaks (BSY polled at
// 0x0000) and plays SN music; the mix stays inside AUDIO_MAX. The mix of
// frames 600-1500 goes to .pio/yiear_attract.wav (scaled x 64) for listening.
static void test_attract_audio(void) {
  boot();
  machine->reset();
  static int16_t wav[900 * 6 * AUDIO_CHUNK];
  int speaking = 0, peak = 0, n = 0;
  for (int f = 0; f < 1500; f++) {
    machine->run_frame();
    speaking += machine->vlm.bsy();
    for (int c = 0; c < 6; c++) {
      int16_t out[AUDIO_CHUNK];
      machine->renderAudio(out, AUDIO_CHUNK);
      for (int i = 0; i < AUDIO_CHUNK; i++) {
        peak = abs(out[i]) > peak ? abs(out[i]) : peak;
        if (f >= 600) { wav[n++] = out[i] * 64; }
      }
    }
  }
  writeWav(".pio/yiear_attract.wav", wav, n);
  printf("speaking frames %d, peak %d\n", speaking, peak);
  TEST_ASSERT_TRUE(speaking > 0);
  TEST_ASSERT_TRUE(peak > 0 && peak <= AUDIO_MAX);
}
#endif

int main(int argc, char **argv) {
  UNITY_BEGIN();
#ifdef HAVE_ROM
  RUN_TEST(test_vlm_phrases);
  RUN_TEST(test_attract_audio);
#endif
  return UNITY_END();
}
