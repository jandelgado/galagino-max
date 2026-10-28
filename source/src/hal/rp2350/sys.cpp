#ifdef HAL_RP2350
#include "sys.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <hardware/clocks.h>
#include <hardware/watchdog.h>
#include <pico/rand.h>
#include <pico/stdlib.h>
#include <pico/version.h>

extern "C" char __StackLimit, __bss_end__;

namespace hal {

static const uint32_t CRASH_MAGIC = 0xC4A5;

void sysInfo(SysInfo &out) {
  out.chip = "RP2350";
  out.revision = rp2350_chip_version();
  out.cores = 2;
  out.cpuMhz = clock_get_hz(clk_sys) / 1000000;
  out.flashBytes = PICO_FLASH_SIZE_BYTES;
  out.sdkVersion = PICO_SDK_VERSION_STRING;
}

MemInfo memInfo() {
  // free blocks inside the heap plus the untouched gap up to the stack limit
  const struct mallinfo mi = mallinfo();
  const uint32_t gap = (uint32_t)(&__StackLimit - (char *)sbrk(0));
  const uint32_t free = (uint32_t)mi.fordblks + gap;
  return MemInfo{free, free, free};
}

MemInfo memInfoData() { return memInfo(); }

void memDump() {
  const MemInfo m = memInfo();
  printf("heap free %u\n", (unsigned)m.free);
}

void *allocSpare(uint32_t bytes) { return malloc(bytes); }

void consoleInit() {
  stdio_init_all();
  for (uint32_t waited = 0; !stdio_usb_connected() && waited < 2000; waited += 10) {
    sleep_ms(10);
  }
}

void printPlatformInfo() {
  if ((watchdog_hw->scratch[0] >> 8) == CRASH_MAGIC) {
    printf("LAST CRASH kind %u a=0x%08x b=0x%08x\n", (unsigned)(watchdog_hw->scratch[0] & 0xFF),
           (unsigned)watchdog_hw->scratch[1], (unsigned)watchdog_hw->scratch[2]);
    // the message is a format string in flash: valid while the image is the same
    const uint32_t msg = watchdog_hw->scratch[2];
    if ((watchdog_hw->scratch[0] & 0xFF) == CRASH_PANIC && msg >= XIP_BASE &&
        msg < XIP_BASE + PICO_FLASH_SIZE_BYTES) {
      printf("LAST CRASH panic: %.80s\n", (const char *)msg);
    }
    watchdog_hw->scratch[0] = 0;
  }
  SysInfo s;
  sysInfo(s);
  printf("%s rev %u, %u cores @ %u MHz, flash %u KB, pico-sdk %s\n", s.chip, (unsigned)s.revision,
         (unsigned)s.cores, (unsigned)s.cpuMhz, (unsigned)(s.flashBytes / 1024), s.sdkVersion);
  memDump();
}

bool imageVerify(bool &supported) {
  supported = false;
  return false;
}

uint32_t random32() { return get_rand_32(); }

void crashReboot(uint32_t kind, uint32_t a, uint32_t b) {
  watchdog_hw->scratch[0] = (CRASH_MAGIC << 8) | kind;
  watchdog_hw->scratch[1] = a;
  watchdog_hw->scratch[2] = b;
  watchdog_reboot(0, 0, 0);
  for (;;) {
  }
}

bool resetWasPanic() { return watchdog_caused_reboot(); }

} // namespace hal
#endif // HAL_RP2350
