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
#include <Arduino.h>
#include <Esp.h>
#include <esp_flash.h>
#include <rom/spi_flash.h>
#include "config.h"
#include "machines.h"
#include "machines/machineBase.h"
#include "emulation/arena.h"
#include "emulation/audio.h"
#include "emulation/video.h"
#include "emulation/input.h"
#include "machines/menu/menu.h"
#include "emulation/emulation.h"
#include "emulation/hud.h"
#include "machines/selftest/selftest.h"
#ifdef ASSETS_REMOTE
  #include "net/loader.h"
#endif
#ifdef LED_PIN
  #include "emulation/led.h"
#endif

signed char machinesCount = (signed char)countof(machines);

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
Video video = Video();
Input input = Input();
#ifdef LED_PIN
  Led led = Led();
#endif

void updateAudioVideo(void);
void renderRow(short row);
void onDoAttractReset();
void onVolumeUpDown(bool up, bool down);
void onDoReset();
bool doReset = false;

// defined in Esp.cpp
uint32_t ESP_getFlashChipId(void);

#ifdef HEAP_PROBE
#include <esp_heap_caps.h>
// heap totals plus per region (DRAM blocks, see arena.h)
static void heapProbeDump(const char *tag) {
  printf("PROBE %s free=%u largest=%u minfree=%u\n", tag,
         heap_caps_get_free_size(MALLOC_CAP_8BIT),
         heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
         heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
  heap_caps_print_heap_info(MALLOC_CAP_8BIT);
}
#endif

void setup() {
#ifdef HEAP_PROBE
  delay(500);  // serial monitor attaches after reset
  heapProbeDump("before-arena");
#endif
  // before anything fragments the heap
  Arena::init();
#ifdef HEAP_PROBE
  heapProbeDump("after-arena");
#endif

  #if CONFIG_IDF_TARGET_ESP32S3
  delay(2000); // USB delay
  #endif

  Serial.begin(115200);
  delay(200); // let serial initialize
  printf("Galagino\n");

  printf("ESP-IDF:     %s\n", ESP.getSdkVersion());
  printf("Arduino:     %d.%d.%d\n", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  printf("ESP Chip:    %s - %d\n", ESP.getChipModel(), ESP.getChipRevision());
  printf("CPU Clock:   %d MHz\n", ESP.getCpuFreqMHz());
  printf("Flash Size:  %d MiB\n", ESP.getFlashChipSize() / 1024 / 1024);
  printf("Flash Speed: %d MHz\n", ESP.getFlashChipSpeed() / 1000 / 1000);
  printf("Flash Mode:  %d - ", ESP.getFlashChipMode());
  switch (ESP.getFlashChipMode()) {
    case FM_QIO:       printf("FM_QIO - Quad IO\n"); break;
    case FM_QOUT:      printf("FM_QOUT - Quad Out\n"); break;
    case FM_DIO:       printf("FM_DIO - Dual IO\n"); break;
    case FM_DOUT:      printf("FM_DOUT - Dual Out\n"); break;
    case FM_FAST_READ: printf("FM_FAST_READ\n"); break;
    case FM_SLOW_READ: printf("FM_SLOW_READ\n"); break;
    default:           printf("FM_UNKNOWN\n"); break;
  }
  printf("Flash Manufacturer: 0x%02x\n", (g_rom_flashchip.device_id >> 16) & 0xff);
  printf("Flash Type Code:    0x%02x\n", (g_rom_flashchip.device_id >> 8) & 0xff);
  printf("Flash Size Code:    0x%02x\n", (g_rom_flashchip.device_id >> 0) & 0xff);
  printf("Flash Id:           0x%08x\n", ESP_getFlashChipId());

#ifdef WORKAROUND_I2S_APLL_PROBLEM
  printf("I2S APLL workaround active\n");
#endif

  printf("Free heap: %d\n", ESP.getFreeHeap());
  printf("Main core: %d\n", xPortGetCoreID());
  printf("Main priority: %d\n", uxTaskPriorityGet(NULL));


  #ifdef CONFIG_IDF_TARGET_ESP32
  printf("CONFIG_IDF_TARGET_ESP32:   defined\n");
  #endif
  #ifdef CONFIG_IDF_TARGET_ESP32S2
  printf("CONFIG_IDF_TARGET_ESP32S2: defined\n");
  #endif
  #ifdef CONFIG_IDF_TARGET_ESP32S3
  printf("CONFIG_IDF_TARGET_ESP32S3: defined\n");
  #endif
  printf("TFT_SPI_HOST:  %d\n", TFT_SPI_HOST);

  switch (TFT_SPI_HOST) {
    case SPI1_HOST: printf("TFT_SPI_HOST:  %s\n", "SPI1_HOST"); break;
    case SPI2_HOST: printf("TFT_SPI_HOST:  %s\n", "SPI2_HOST"); break; // esp32 HSPI | esp32-s3 FSPI
    case SPI3_HOST: printf("TFT_SPI_HOST:  %s\n", "SPI3_HOST"); break; // esp32 VSPI | esp32-s3 HSPI
    default:        printf("TFT_SPI_HOST:  %s\n", "?");         break;
  }
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
  #ifdef AUDIO_ENABLE_PIN
  printf("AUDIO_ENABLE_PIN: %d\n", AUDIO_ENABLE_PIN);
  #endif

  #ifdef AUDIO_ENABLE_PIN
  pinMode(AUDIO_ENABLE_PIN, OUTPUT);   // ESP32-E Audio Enable
  digitalWrite(AUDIO_ENABLE_PIN, LOW); // active low
  printf("AUDIO_ENABLE:     %s\n", "LOW");
  #endif

  // permanent buffers: largest first, so none has to fit a hole
  memory = (uint8_t *)malloc(RAMSIZE);
  if (!memory) {
    // DRAM statics grew (e.g. a new global): the heap block left after
    // the Arena is smaller than RAMSIZE
    printf("fatal: malloc(RAMSIZE=%d) failed, MaxAlloc=%d Free=%d\n", RAMSIZE,
           ESP.getMaxAllocHeap(), ESP.getFreeHeap());
    abort();
  }
  frame_buffer = (unsigned short*)malloc(240 * 8 * 2);
  sprite_buffer = (sprite_S*)malloc(128 * sizeof(sprite_S));
  audio.init();

  input.init(machinesCount == 1);
  input.onVolumeUpDown(onVolumeUpDown);
  input.onDoReset(onDoReset);
  input.onDoAttractReset(onDoAttractReset);

#ifdef LED_PIN
  led.init();
#endif

  video.begin();
  printf("setup() Heap: Free=%d MaxAlloc=%d MinFree=%d\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
}

void loop(void) {
  // run video in main task. This will send signals to the emulation task in the background to synchronize video
  updateAudioVideo();

#ifdef LED_PIN
  led.update(currentMachine);
#endif
}

// max time between full rate frames giving the idle task CPU time
static const uint32_t IDLE_YIELD_MS = 1000;
// full rate frame period, 60.0 Hz
static const uint32_t FRAME_US = 16667;
// further behind the frame deadline than this: restart pacing
static const int32_t MAX_LATE_US = 50000;

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
    if (!Loader::prepare(machines[index].bundle)) {
      switchTo(SHOW_MENU);
      return;
    }
#endif
    Arena::reserve(machines[index].arena);
    currentMachine = machines[index].create();
#ifdef MASTER_ATTRACT_GAME_TIMEOUT
    attractGameStart = machinesCount > 1 ? millis() : 0;
#endif
  }

  currentMachine->init(&input, frame_buffer, sprite_buffer, memory);
  audio.start(currentMachine);
  video.flip(currentMachine->videoFlipY(), currentMachine->videoFlipX());
  emulation_start();
  printf("switchTo(%d) Heap: Free=%d MaxAlloc=%d MinFree=%d\n", index, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
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
  RTC_NOINIT_ATTR static uint32_t probeMagic;
  RTC_NOINIT_ATTR static int8_t probeIndex;
  static uint32_t probeStart = 0;

  if (!currentMachine) {
    if (probeMagic == PROBE_MAGIC && esp_reset_reason() == ESP_RST_PANIC) {
      printf("PROBE game=%d type=%d FAIL\n", probeIndex, machines[probeIndex].type);
    }
    else {
      probeIndex = -1;
    }
    probeMagic = PROBE_MAGIC;
  }

  if (currentMachine && millis() - probeStart <= PROBE_GAME_MS) {
    return false;
  }

  if (currentMachine) {
    printf("PROBE game=%d type=%d arena=%u free=%u largest=%u minfree=%u\n", probeIndex,
           machines[probeIndex].type, Arena::bytesUsed(),
           heap_caps_get_free_size(MALLOC_CAP_8BIT),
           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
           heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
  }
  probeIndex++;
  if (probeIndex == machinesCount) {
    heapProbeDump("end");
    probeMagic = 0;
    probeIndex = 0;
  }

  switchTo(SHOW_MENU);  // menu dtor resets the Arena
  switchTo(probeIndex);
  probeStart = millis();
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
  if (millis() - attractGameStart <= MASTER_ATTRACT_GAME_TIMEOUT) {
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
  static uint32_t last_yield_ms = 0;
  uint32_t t0 = micros();

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

  bool videoHalfRate = true;
#ifndef VIDEO_HALF_RATE
  videoHalfRate = currentMachine->useVideoHalfRate();
#endif
  const int renderWidth = currentMachine->renderWidth();
  const int renderWrite = renderWidth << 3;
  video.setViewport(renderWidth);

  if (!videoHalfRate) {
    // render and transmit screen at once as the display running at 80Mhz can update at full 60 hz game frame
    for(int c = 0; c < 36; c += 6) {
      for (int i = 0; i < 6; i++) {
        renderRow(c + i); video.write(frame_buffer, renderWrite);
      }

      // audio is updated 6 times per 60 Hz frame
      audio.transmit();
    }

    emulation_videoRendered();

    // Deadline pacing at 60.0 Hz: delays alternate 16/17/17 ms. A fixed 16 ms
    // pad ran at 62.5 Hz, games 4% fast, and beat against the 60 Hz panel: a
    // skipped frame (scroll jerk) about every second.
    static uint32_t next_us = 0;
    next_us += FRAME_US;
    const int32_t wait_us = (int32_t)(next_us - micros());
    if(wait_us > 0) {
      vTaskDelay((wait_us + 999) / 1000);
      last_yield_ms = millis();
    }
    else {
      // far behind (stall, machine start): restart, no catch-up burst
      if(wait_us < -MAX_LATE_US) {
        next_us = micros();
      }

      // Idle task needs CPU time within the task watchdog timeout. Not every
      // frame: a 1 tick delay pushes a late frame further behind.
      if(millis() - last_yield_ms > IDLE_YIELD_MS) {
        vTaskDelay(1);
        last_yield_ms = millis();
      }
    }

    // physical refresh is 60Hz. So send vblank trigger once a frame
    emulation_notifyGive();
  }
  else {
    // render and transmit screen in two halfs as the display running at 40Mhz can only update every second 60 hz game frame
    for(int half = 0; half < 2; half++) {
      for(int c = 18 * half; c < 18 * (half + 1); c += 3) {
        renderRow(c + 0); video.write(frame_buffer, renderWrite);
        renderRow(c + 1); video.write(frame_buffer, renderWrite);
        renderRow(c + 2); video.write(frame_buffer, renderWrite);

        // audio is refilled 6 times per screen update. The screen is updated
        // every second frame. So audio is refilled 12 times per 30 Hz frame.
        // Audio registers are updated by CPU3 two times per 30hz frame.
        audio.transmit();
      }

      emulation_videoRendered();

      // one screen at 60 Hz is 16.6ms
      unsigned long t1 = (micros() - t0) / 1000;  // calculate time in milliseconds
      if(t1 < (half ? 33 : 16))
        vTaskDelay((half ? 33 : 16) - t1);
      else if(half)
        vTaskDelay(1);    // at least 1 ms delay to prevent watchdog timeout

      // physical refresh is 30Hz. So send vblank trigger twice a frame to the emulation. This will make the game run with 60hz speed
      emulation_notifyGive();
    }
  }
}

// render one of 36 strips (8 lines, width supplied by the active machine)
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
