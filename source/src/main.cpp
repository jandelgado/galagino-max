/*
 * Galagino - Galaga arcade for ESP32 and Platformio
 *
 * (c) 2025 speckhoiler
 *
 * This is a port of Till Harbaum's awesome Galaga emulator
 * https://github.com/harbaum/galagino
 *
 * Published under GPLv3
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "hal/hal.h"
#include "hal/entry.h"
#include "machines.h"
#include "machines/machineBase.h"
#include "emulation/arena.h"
#include "emulation/audio.h"
#include "hal/video.h"
#include "hal/video_width.h"
#include "emulation/input.h"
#include "machines/menu/menu.h"
#include "emulation/emulation.h"
#include "emulation/hud.h"
#include "machines/selftest/selftest.h"
#ifdef ASSETS_REMOTE
  #include "net/loader.h"
  #include "ui/loader_screen.h"
#endif
#ifdef LED_PIN
  #include "emulation/led.h"
#endif

int16_t machinesCount = countof(machines);

machineBase *currentMachine;

// non-null while the menu is the running machine
Menu *menu;

// switchTo() argument for the menu
static const int8_t SHOW_MENU = -1;

#ifdef BOOT_SELFTEST
// switchTo() argument for the boot self-test
static const int8_t SHOW_SELFTEST = -2;

// non-null while the boot self-test is the running machine
SelfTest *selftest;
#endif

// last started game, the menu strip starts there. Boot: second logo, centred
static int8_t lastIndex = 1;

// highlighted when the menu opens: lastIndex, or the next game after an
// attract timeout (the strip then scrolls one logo)
static int8_t menuPreselect = 1;

#ifdef MASTER_ATTRACT_GAME_TIMEOUT
// game start time, 0 = disabled by player input
static uint32_t attractGameStart = 0;
#endif

// the hardware supports 64 sprites
struct sprite_S *sprite_buffer;

// buffer space for one row of 28 characters
unsigned short *frame_buffer;

// RAM
unsigned char *memory;

Audio audio;  // not copy-initialized: holds atomics (ima_adpcm::Voice)
hal::Video video;
static_assert(hal::Video::CANVAS_W >= 240 && hal::Video::CANVAS_H >= 288,
              "video canvas smaller than the 240 x 288 strip picture");
static_assert(hal::Video::MAX_WIDTH == hal::VIDEO_MAX_WIDTH &&
              hal::Video::CANVAS_W >= hal::VIDEO_MAX_WIDTH,
              "video_width.h disagrees with the video backend");
Input input = Input();
#ifdef LED_PIN
  Led led = Led();
#endif

void updateAudioVideo(void);
void renderRow(short row);
#ifdef HAL_VIDEO_NATIVE
static void renderNative(uint32_t t0);
#endif
void onDoAttractReset();
void onVolumeUpDown(bool up, bool down);
void onDoReset();
bool doReset = false;

#ifdef HEAP_PROBE
// heap totals plus per region (DRAM blocks, see arena.h)
static void heapProbeDump(const char *tag) {
  const hal::MemInfo m = hal::memInfoData();
  printf("PROBE %s free=%u largest=%u minfree=%u\n", tag, m.free, m.largest, m.minFree);
  hal::memDump();
}
#endif

void galagino_setup(void) {
#ifdef HEAP_PROBE
  hal::sleepMs(500);  // serial monitor attaches after reset
  heapProbeDump("before-arena");
#endif
  // before anything fragments the heap
  Arena::init();
#ifdef HEAP_PROBE
  heapProbeDump("after-arena");
#endif

  hal::consoleInit();
  printf("Galagino\n");

  hal::printPlatformInfo();

#ifdef HAL_VIDEO_SPI_TFT
  printf("TFT_SPI_PORT:  SPI%d\n", TFT_SPI_PORT);  // esp32 SPI2 HSPI | esp32-s3 FSPI, SPI3 VSPI | HSPI
  printf("TFT Controller: ");
  #ifdef TFT_ILI9341
  printf("ILI9341\n");
  #else
  printf("ST7789\n");
  #endif
  printf("TFT SPI Clock:  %d MHz\n", TFT_SPICLK/1000000);
  printf("TFT_CS:     %d\n", TFT_CS);
  printf("TFT_DC:     %d\n", TFT_DC);
  printf("TFT_BL:     %d\n", TFT_BL);
  printf("TFT_MISO:   %d\n", TFT_MISO);
  printf("TFT_MOSI:   %d\n", TFT_MOSI);
  printf("TFT_SCLK:   %d\n", TFT_SCLK);
  printf("TFT_RST:    %d\n", TFT_RST);
  printf("TFT_VFLIP:  %s\n",
  #ifdef TFT_VFLIP
  "ON"
  #else
  "OFF"
  #endif
  );
  printf("TFT_INVERT: %s\n", 
  #ifdef TFT_INVERT
  "ON"
  #else
  "OFF"
  #endif
  );
#endif

  #ifdef AUDIO_ENABLE_PIN
  printf("AUDIO_ENABLE_PIN: %d\n", AUDIO_ENABLE_PIN);
  #endif

  #ifdef AUDIO_ENABLE_PIN
  hal::pinOutput(AUDIO_ENABLE_PIN, false); // ESP32-E Audio Enable, active low
  printf("AUDIO_ENABLE:     %s\n", "LOW");
  #endif

  // permanent buffers: memory (largest) first in the main heap, the strip
  // buffers in the spare pool, so A's region keeps its bytes for the Arena
  memory = (uint8_t *)malloc(RAMSIZE);
  if (!memory) {
    // DRAM statics grew (e.g. a new global): the heap block left after
    // the Arena is smaller than RAMSIZE
    const hal::MemInfo m = hal::memInfo();
    printf("fatal: malloc(RAMSIZE=%d) failed, MaxAlloc=%d Free=%d\n", RAMSIZE,
           (int)m.largest, (int)m.free);
    abort();
  }
  frame_buffer = (unsigned short*)hal::allocSpare(hal::VIDEO_MAX_WIDTH * 8 * 2);
  sprite_buffer = (sprite_S*)hal::allocSpare(128 * sizeof(sprite_S));
#ifdef HEAP_PROBE
  heapProbeDump("after-buffers");
#endif
  audio.init();

  input.init(machinesCount == 1);
  input.onVolumeUpDown(onVolumeUpDown);
  input.onDoReset(onDoReset);
  input.onDoAttractReset(onDoAttractReset);

#ifdef LED_PIN
  led.init();
#endif

  video.begin();
  const hal::MemInfo heap = hal::memInfo();
  printf("setup() Heap: Free=%d MaxAlloc=%d MinFree=%d\n", (int)heap.free, (int)heap.largest, (int)heap.minFree);
}

void galagino_loop(void) {
  // run video in main task. This will send signals to the emulation task in the background to synchronize video
  updateAudioVideo();

#ifdef LED_PIN
  led.update(currentMachine);
#endif
}

// Replace the running machine (game, menu or self-test). The old one is deleted
// first: the menu ctor resets Arena, which must not hold live assets.
static void switchTo(int8_t index) {
  if (currentMachine) {
    emulation_stop();
    video.flipReset(currentMachine->videoFlipY(), currentMachine->videoFlipX());
    audio.stop();
    delete currentMachine;
    // a failed game load re-enters switchTo() for the menu
    currentMachine = nullptr;
  }

  menu = nullptr;
#ifdef BOOT_SELFTEST
  selftest = nullptr;
#endif
  if (index == SHOW_MENU) {
    menu = new Menu(machines, machinesCount, lastIndex, menuPreselect);
    currentMachine = menu;
  }
#ifdef BOOT_SELFTEST
  else if (index == SHOW_SELFTEST) {
    selftest = new SelfTest(&input, machinesCount);
    currentMachine = selftest;
  }
#endif
  else {
    lastIndex = index;
    menuPreselect = index;
#ifdef ASSETS_REMOTE
    LoaderScreen screen(video, frame_buffer, input);
    if (!Loader::prepare(machines[index].bundle, screen)) {
      switchTo(SHOW_MENU);
      return;
    }
#endif
    Arena::reserve(machines[index].arena);
    currentMachine = machines[index].create();
#ifdef MASTER_ATTRACT_GAME_TIMEOUT
    attractGameStart = machinesCount > 1 ? hal::millis() : 0;
#endif
  }

  currentMachine->init(&input, frame_buffer, sprite_buffer, memory);
  audio.start(currentMachine);
  video.flip(currentMachine->videoFlipY(), currentMachine->videoFlipX());
  emulation_start();
  const hal::MemInfo heap = hal::memInfo();
  printf("switchTo(%d) Heap: Free=%d MaxAlloc=%d MinFree=%d\n", index, (int)heap.free, (int)heap.largest, (int)heap.minFree);
}

// HEAP_PROBE build: runs every machine PROBE_GAME_MS and reports Arena
// and heap use, to size the Arena. True if it switched machines.
static bool heapProbeStep() {
#ifdef HEAP_PROBE
#ifndef HEAP_PROBE_GAME_MS
#define HEAP_PROBE_GAME_MS 5000
#endif
  static const uint32_t PROBE_GAME_MS = HEAP_PROBE_GAME_MS;
  // survive the abort and reboot of an Arena overflow: the sweep goes on
  // with the next game
  static const uint32_t PROBE_MAGIC = 0x50524f42;
  HAL_RETAINED static uint32_t probeMagic;
  HAL_RETAINED static int8_t probeIndex;
  static uint32_t probeStart = 0;

  if (!currentMachine) {
    // index checked: RTC memory survives flashing a firmware with fewer games
    if (probeMagic == PROBE_MAGIC && hal::resetWasPanic() &&
        probeIndex >= 0 && probeIndex < machinesCount) {
      printf("PROBE game=%d type=%d FAIL\n", probeIndex, machines[probeIndex].type);
    }
    else {
      probeIndex = -1;
    }
    probeMagic = PROBE_MAGIC;
  }

  if (currentMachine && hal::millis() - probeStart <= PROBE_GAME_MS) {
    return false;
  }

  if (currentMachine) {
    const hal::MemInfo m = hal::memInfoData();
    printf("PROBE game=%d type=%d arena=%u free=%u largest=%u minfree=%u\n", probeIndex,
           machines[probeIndex].type, Arena::bytesUsed(), m.free, m.largest, m.minFree);
  }
  // a panic while leaving the game still counts for it
  switchTo(SHOW_MENU);  // menu dtor resets the Arena
  probeIndex++;
  if (probeIndex == machinesCount) {
    heapProbeDump("end");
    probeMagic = 0;
    probeIndex = 0;
  }
  switchTo(probeIndex);
  probeStart = hal::millis();
  return true;
#else
  return false;
#endif
}

// Idle game for too long: back to the menu with the next game preselected,
// menu attract then starts it.
static bool attractGameTimeout() {
#ifdef MASTER_ATTRACT_GAME_TIMEOUT
  if (menu || !attractGameStart) {
    return false;
  }
  if (hal::millis() - attractGameStart <= MASTER_ATTRACT_GAME_TIMEOUT) {
    return false;
  }

  menuPreselect = (lastIndex + 1) % machinesCount;
  printf("MASTER ATTRACT game timeout, return to menu\n");
  return true;
#else
  return false;
#endif
}

void updateAudioVideo(void) {
  uint32_t t0 = hal::micros();

  const int8_t first = machinesCount == 1 ? 0 : SHOW_MENU;
  if (heapProbeStep()) {
    // probe build: the probe picks the machine
  }
  else if (!currentMachine) {
#ifdef BOOT_SELFTEST
    switchTo(SHOW_SELFTEST);
#else
    switchTo(first);
#endif
  }
#ifdef BOOT_SELFTEST
  else if (selftest && selftest->done()) {
    switchTo(first);
  }
#endif
  else if (doReset || attractGameTimeout()) {
    doReset = false;
    switchTo(SHOW_MENU);
  }
  else if (menu && menu->selected() != Menu::NONE) {
    switchTo(menu->selected());
  }

  currentMachine->prepare_frame();

#ifdef HAL_VIDEO_NATIVE
  if (currentMachine->renderRows() == hal::VIDEO_NATIVE_H) {
    renderNative(t0);
    return;
  }
#endif

  video.setHalfRate(currentMachine->useVideoHalfRate());
  const int renderWidth = currentMachine->renderWidth();
  const int renderWrite = renderWidth << 3;
  video.setViewport(renderWidth);

  // A screen is sent in screenParts() parts: one at full rate (the display can
  // update at the 60 Hz game frame), two at half rate (the display updates
  // every second game frame; the emulation still runs at 60 Hz, so it is
  // notified after each part).
  const uint8_t parts = video.screenParts();
  const int stripsPerPart = 36 / parts;
  // audio is refilled 6 times per screen update
  // (half rate: 12 times per 30 Hz frame; audio registers are updated by CPU3
  // two times per 30 Hz frame)
  const int stripsPerAudio = 36 / 6 / parts;

  for (uint8_t part = 0; part < parts; part++) {
    for (int c = stripsPerPart * part; c < stripsPerPart * (part + 1); c += stripsPerAudio) {
      for (int i = 0; i < stripsPerAudio; i++) {
        renderRow(video.stripIndex(c + i)); video.submit(frame_buffer, renderWrite);
      }
      audio.transmit();
    }

    emulation_videoRendered();

    // returns when the next part may start: deadline pacing on a panel
    video.endFrame(part, t0);

    // physical refresh: tell the emulation a vblank passed
    emulation_notifyGive();
  }
}

void renderRow(short row) {
  memset(frame_buffer, 0, currentMachine->renderBuffer());
  currentMachine->render_row(row);
#ifdef DEBUG_TIMING_FPS_HUD
  uint8_t flip = (currentMachine->videoFlipY() ? HUD_FLIP_Y : HUD_FLIP_NONE)
               | (currentMachine->videoFlipX() ? HUD_FLIP_X : HUD_FLIP_NONE);
  hud_render_fps(frame_buffer, currentMachine->renderWidth(), row, flip);
#endif
}

void onVolumeUpDown(bool up, bool down) {
  audio.volumeUpDown(up, down);
}

// in-game input disarms the game attract timeout; the menu tracks its own
void onDoAttractReset() {
#ifdef MASTER_ATTRACT_GAME_TIMEOUT
  if (attractGameStart) {
    attractGameStart = 0;
    printf("MASTER ATTRACT timer reset\n");
  }
#endif
}

void onDoReset() {
  if (!menu) {
    doReset = true;
  }
}

#ifdef HAL_VIDEO_NATIVE
// Native screen (vector games on HDMI): 60 strips rendered straight into the
// scanout ring, just ahead of the beam. One screen per scanout pass.
static void renderNative(uint32_t t0) {
  const uint32_t strips = hal::Video::NATIVE_STRIPS;
  const uint32_t stripsPerAudio = strips / 6;
  video.setNative(currentMachine->renderWidth());

  const int bytes = currentMachine->renderBuffer();
  for (uint32_t s = 0; s < strips; s++) {
    uint16_t *strip = video.nativeStrip();
    memset(strip, 0, bytes);
    currentMachine->setFrameBuffer(strip);
    currentMachine->render_row(s);
    video.nativeSubmit();
    if ((s + 1) % stripsPerAudio == 0) {
      audio.transmit();
    }
  }
  currentMachine->setFrameBuffer(frame_buffer);

  emulation_videoRendered();
  video.endFrame(0, t0);
  emulation_notifyGive();
}
#endif
