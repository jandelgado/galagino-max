#include <stdio.h>
#include "../hal/hal.h"
#include "emulation.h"
#include "input.h"
#include "../machines/machineBase.h"

// A hung emulation task panics after this many seconds without a frame.
static const uint32_t EMULATION_WDT_TIMEOUT_S = 8;

// including of "../machines/machineBase.h" in "emulation.h" not possible
extern machineBase *currentMachine;
extern Input input;
static hal::TaskHandle emulationTaskHandle;
static hal::Signal frameSignal;  // video core -> emulation core: next frame
volatile static char doDeleteEmulationTask;

#if defined(DEBUG_TIMING) || defined(DEBUG_TIMING_FPS_HUD)
#define EMULATION_MEASURE_FPS
static uint32_t counter;
static uint32_t timeTotal = 0;
static uint32_t cpuStart;
static uint32_t cpuSum = 0;
static uint32_t videoSum = 0;
#endif

#ifdef DEBUG_TIMING_FPS_HUD
volatile uint16_t emulation_fps;
#endif

void emulation_start() {
#ifdef EMULATION_MEASURE_FPS
  timeTotal = hal::millis();
#endif
  currentMachine->reset();
  currentMachine->start();
  frameSignal.clear();  // a frame signalled for the previous machine is stale
  emulationTaskHandle = hal::taskSpawn(emulation_task, NULL, "emulation task", 4096, 2, hal::EMULATION_CORE);
  hal::watchdogWatch(emulationTaskHandle, EMULATION_WDT_TIMEOUT_S);
}

void emulation_stop() {
  if (!emulationTaskHandle)
    return;
  
  input.disable(); // disable input read from nunchuck
  doDeleteEmulationTask = 1;
  while (doDeleteEmulationTask) {
    frameSignal.give();
    hal::sleepMs(1);
  }

  emulationTaskHandle = NULL;
  currentMachine->reset();  // clear sound output

  input.enable(); // enable input read from nunchuck
}

void emulation_notifyGive() {
  if (!emulationTaskHandle)
    return;

  frameSignal.give();
}

void emulation_videoRendered(void) {
#ifdef EMULATION_MEASURE_FPS
  videoSum += hal::millis() - cpuStart;
#endif
}

HAL_FAST_CODE void emulation_task(void *p) {  
  currentMachine->start();

  for(;;) {
#ifdef EMULATION_MEASURE_FPS
    cpuStart = hal::millis();
#endif

    currentMachine->run_frame();
    hal::watchdogFeed();

    if (doDeleteEmulationTask) {
      doDeleteEmulationTask = 0;
      hal::watchdogUnwatch(emulationTaskHandle);
      hal::taskExit();
    }

#ifdef EMULATION_MEASURE_FPS
    cpuSum += hal::millis() - cpuStart;

    // The 60hz vblank rate is in turn 16.6 ms.
  if (counter % 10 == 0) {
    // good time total: 160...170ms
    uint32_t now = cpuStart;
    uint32_t dt = now - timeTotal;
    uint32_t hz = dt ? 10000 / dt : 0;
#ifdef DEBUG_TIMING_FPS_HUD
    emulation_fps = hz;
#endif
#ifdef DEBUG_TIMING
    printf("10-frames: %3u Hz | Total: %3u ms | Cpu: %3u ms | Video: %3u ms\n", hz, dt, cpuSum, videoSum);
#endif
    timeTotal = now;
    cpuSum = 0;
    videoSum = 0;
  }
  counter++;
#endif
   
  // Wait for signal from video task to emulate a 60Hz frame rate. Don't do
  // this unless the game has actually started to speed up the boot process
  // a little bit.
  if(currentMachine->game_started)
    frameSignal.take(hal::WAIT_FOREVER);
  else
    hal::sleepMs(1); // give a millisecond delay to make the watchdog happy
  }
}

unsigned char OpZ80_INL(unsigned short Addr) {
  return currentMachine->opZ80(Addr);
}

unsigned char OpPrefixZ80_INL(unsigned short Addr) {
  return currentMachine->opPrefixZ80(Addr);
}

void OutZ80(unsigned short Port, unsigned char Value) {
  currentMachine->outZ80(Port, Value);
}
  
unsigned char InZ80(unsigned short Port) {
  return currentMachine->inZ80(Port);
}

void WrZ80(unsigned short Addr, unsigned char Value) {
  currentMachine->wrZ80(Addr, Value);
}

unsigned char RdZ80(unsigned short Addr) {
  return currentMachine->rdZ80(Addr);
}

void PatchZ80(Z80 *R) {
}

void i8048_port_write(i8048_state_S *state, unsigned char port, unsigned char pos) {
  currentMachine->wrI8048_port(state, port, pos);
}

unsigned char i8048_port_read(i8048_state_S *state, unsigned char port) {
  return currentMachine->rdI8048_port(state, port);
}

unsigned char i8048_rom_read(i8048_state_S *state, unsigned short addr) {
  return currentMachine->rdI8048_rom(state, addr);
}

unsigned char i8048_xdm_read(i8048_state_S *state, unsigned char addr) {
  return currentMachine->rdI8048_xdm(state, addr);
}

void i8048_xdm_write(i8048_state_S *state, unsigned char addr, unsigned char data) {
  currentMachine->wrI8048_xdm(state, addr, data);
}

unsigned char m6809_read(m6809_state *s, uint16_t addr) {
  return currentMachine->m6809_read(s, addr);
}

void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  currentMachine->m6809_write(s, addr, val);
}

unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return currentMachine->m6809_read_opcode(s, addr);
}
