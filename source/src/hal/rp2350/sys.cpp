#ifdef HAL_RP2350
#include "sys.h"
#include "rtos.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <hardware/clocks.h>
#include <hardware/watchdog.h>
#include <pico/critical_section.h>
#include <pico/rand.h>
#include <pico/stdio/driver.h>
#include <pico/stdio_usb.h>
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

namespace {

// TinyUSB is not multicore safe: a write from the emulation core while the
// USB IRQ runs on the video core panics ("Can't continue xfer on inactive
// ep"). Output of the other core goes through this ring; a task on the
// video core passes it to USB.
const uint32_t RING_SIZE = 2048;
const uint32_t DRAIN_MS = 10;
char ring[RING_SIZE];
uint32_t ring_head;  // next write
uint32_t ring_tail;  // next read
critical_section_t ring_lock;

void consoleOut(const char *buf, int len) {
  if ((int)get_core_num() == VIDEO_CORE) {
    stdio_usb.out_chars(buf, len);
    return;
  }
  // full ring: the rest of the message is dropped
  critical_section_enter_blocking(&ring_lock);
  for (int i = 0; i < len && ring_head - ring_tail < RING_SIZE; i++) {
    ring[ring_head++ % RING_SIZE] = buf[i];
  }
  critical_section_exit(&ring_lock);
}

void consoleFlush() {
  if ((int)get_core_num() == VIDEO_CORE) {
    stdio_usb.out_flush();
  }
}

int consoleIn(char *buf, int len) { return stdio_usb.in_chars(buf, len); }

void consoleDrain(void *) {
  for (;;) {
    char chunk[64];
    uint32_t n = 0;
    critical_section_enter_blocking(&ring_lock);
    while (n < sizeof(chunk) && ring_tail != ring_head) {
      chunk[n++] = ring[ring_tail++ % RING_SIZE];
    }
    critical_section_exit(&ring_lock);
    if (n) {
      stdio_usb.out_chars(chunk, (int)n);
      continue;
    }
    sleepMs(DRAIN_MS);
  }
}

stdio_driver_t console = {
  .out_chars = consoleOut,
  .out_flush = consoleFlush,
  .in_chars = consoleIn,
  .set_chars_available_callback = nullptr,
  .next = nullptr,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
  .last_ended_with_cr = false,
  .crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

} // namespace

void consoleInit() {
  stdio_init_all();
  // USB output only through the core-safe driver above
  critical_section_init(&ring_lock);
  stdio_set_driver_enabled(&stdio_usb, false);
  stdio_set_driver_enabled(&console, true);
  taskSpawn(consoleDrain, nullptr, "console", 1024, 1, VIDEO_CORE);
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
