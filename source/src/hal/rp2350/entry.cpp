// Program entry: the scheduler starts a main task on core 0 (the video core)
// that runs the application loop.
#ifdef HAL_RP2350
#include "../../config.h"  // the board profile may set GALAGINO_CLOCK_KHZ
#include "../entry.h"
#include <FreeRTOS.h>
#include <task.h>
#include <pico/stdlib.h>
#include <hardware/exception.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>
#include <hardware/watchdog.h>
#include "sys.h"

namespace {

void mainTask(void *) {
  galagino_setup();
  for (;;) {
    galagino_loop();
  }
}

} // namespace

// A crash is recorded in watchdog scratch registers and the chip resets;
// printPlatformInfo() reports it after the reboot.
extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char *name) {
  hal::crashReboot(hal::CRASH_STACK, ((uint32_t)name[0] << 8) | (uint8_t)name[1], 0);
}

extern "C" void hardFaultC(uint32_t *frame) {
  hal::crashReboot(hal::CRASH_FAULT, frame[6], frame[5]);  // pc, lr
}

// PICO_PANIC_FUNCTION (CMakeLists.txt): panic() pushed its caller's lr and
// called this with the message in r0.
extern "C" [[noreturn]] void galaginoPanicRecord(const char *fmt, uint32_t caller) {
  hal::crashReboot(hal::CRASH_PANIC, caller, (uint32_t)fmt);
}

extern "C" __attribute__((naked, noreturn)) void galaginoPanic(const char *, ...) {
  __asm volatile("ldr r1, [sp]\n b galaginoPanicRecord\n");
}

extern "C" __attribute__((naked)) void hardFault() {
  __asm volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b hardFaultC\n");
}

int main() {
#ifdef GALAGINO_CLOCK_KHZ
  // above the default 150 MHz: more core voltage first
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(2);
  set_sys_clock_khz(GALAGINO_CLOCK_KHZ, true);
#endif
  exception_set_exclusive_handler(HARDFAULT_EXCEPTION, hardFault);
  TaskHandle_t handle = nullptr;
  xTaskCreateAffinitySet(mainTask, "main", 8192 / sizeof(StackType_t), nullptr, 1, 1u << 0, &handle);
  vTaskStartScheduler();
  for (;;) {
  }
}
#endif // HAL_RP2350
