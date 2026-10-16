#include <stdio.h>
#include <string.h>
#include <unity.h>

// Track & Field sound on the real ROMs: every VLM5030 phrase of 407_c09
// speaks and ends, and the attract mode drives both SN76489 and VLM5030
// through the machine class. ROM headers come from romconv (./convert.sh
// trackfld); without them the tests are skipped.
#if __has_include("../../src/machines/trackfld/trackfld_rom_main.h")
#define HAVE_ROM 1
#define ENABLE_TRACKFLD
#include "../../src/emulation/lzma/LzmaDec.c"
#include "../../src/emulation/arena.cpp"
#include "../../src/cpus/m6809/m6809.c"
#include "../../src/cpus/z80/Z80.c"
#include "../../src/emulation/sn76489.cpp"
#include "../../src/emulation/vlm5030.cpp"
#define private public
#define protected public
#include "../../src/machines/trackfld/trackfld.cpp"
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
static trackfld *machine;

unsigned int Input::buttons_get(void) { return 0; }
bool Input::demoSoundsOff(void) { return false; }
extern "C" {
unsigned char OpZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char OpPrefixZ80_INL(unsigned short a) { return machine->opZ80(a); }
unsigned char RdZ80(unsigned short a) { return machine->rdZ80(a); }
void WrZ80(unsigned short a, unsigned char v) { machine->wrZ80(a, v); }
unsigned char InZ80(unsigned short p) { return 0xff; }
void OutZ80(unsigned short p, unsigned char v) {}
void PatchZ80(Z80 *) {}
word LoopZ80(Z80 *) { return INT_NONE; }
}
void Reset6502(M6502 *) {}
unsigned char m6809_read(m6809_state *s, uint16_t a) { return machine->m6809_read(s, a); }
unsigned char m6809_read_opcode(m6809_state *s, uint16_t a) { return machine->m6809_read_opcode(s, a); }
void m6809_write(m6809_state *s, uint16_t a, uint8_t v) { machine->m6809_write(s, a, v); }



static void boot(void) {
  if (machine) {
    return;
  }
  Arena::init();
  machine = new trackfld();
  machine->init(&input, frame_buffer, sprites, memory);
  machine->reset();
}

// 62 phrase table entries: the table ends where phrase 0 starts (0x7c)
static void test_vlm_phrases(void) {
  boot();
  const int count = ((machine->vlm_rom[0] << 8) | machine->vlm_rom[1]) / 2;
  TEST_ASSERT_EQUAL_INT(62, count);
  Vlm5030 vlm;
  int shortest = 1 << 30, longest = 0, quietest = 512, loudest = 0;
  for (int p = 0; p < count; p++) {
    vlm.reset(machine->vlm_rom, 0x2000);
    vlm.data_w(p * 2);
    vlm.st_w(true);
    vlm.st_w(false);
    int n = 0, peak = 0;
    while (vlm.bsy() && n < 3 * (int)VLM_HZ) {
      const int s = vlm.sample() >> 6;
      peak = abs(s) > peak ? abs(s) : peak;
      n++;
    }
    TEST_ASSERT_FALSE_MESSAGE(vlm.bsy(), "phrase never ends");
    shortest = n < shortest ? n : shortest;
    longest = n > longest ? n : longest;
    quietest = peak < quietest ? peak : quietest;
    loudest = peak > loudest ? peak : loudest;
  }
  printf("phrases: %d..%d samples, peaks %d..%d\n", shortest, longest, quietest, loudest);
  TEST_ASSERT_TRUE(loudest < 511);  // no clipping
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

// Attract mode through the machine: sound Z80 plays SN / DAC, the announcer
// speaks once (MAME: phrase 0 written at frame 2798, hurdles demo; BSY
// polled at 0xe002); the mix stays inside AUDIO_MAX. The mix of frames
// 2400-3000 goes to .pio/trackfld_attract.wav (scaled x 64) for listening.
static void test_attract_audio(void) {
  boot();
  machine->reset();
  static int16_t wav[600 * 6 * AUDIO_CHUNK];
  int speaking = 0, peak = 0, n = 0, first_speech = -1;
  for (int f = 0; f < 3000; f++) {
    machine->run_frame();
    if (machine->vlm.bsy()) {
      first_speech = first_speech < 0 ? f : first_speech;
      speaking++;
    }
    for (int c = 0; c < 6; c++) {
      int16_t out[AUDIO_CHUNK];
      machine->renderAudio(out, AUDIO_CHUNK);
      for (int i = 0; i < AUDIO_CHUNK; i++) {
        peak = abs(out[i]) > peak ? abs(out[i]) : peak;
        if (f >= 2400) { wav[n++] = out[i] * 64; }
      }
    }
  }
  writeWav(".pio/trackfld_attract.wav", wav, n);
  printf("speaking frames %d from %d, peak %d\n", speaking, first_speech, peak);
  TEST_ASSERT_INT_WITHIN(3, 2797, first_speech);  // MAME frame 2798, counted from 1
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
