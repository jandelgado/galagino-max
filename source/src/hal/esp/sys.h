#ifndef HAL_ESP_SYS_H
#define HAL_ESP_SYS_H

#include <stdint.h>

namespace hal {

struct SysInfo {
  const char *chip;
  uint32_t revision;
  uint32_t cores;
  uint32_t cpuMhz;
  uint32_t flashBytes;
  const char *sdkVersion;
};

struct MemInfo {
  uint32_t free;
  uint32_t largest;   // largest free block
  uint32_t minFree;   // lowest free since boot
};

void sysInfo(SysInfo &out);

// Internal RAM, any capability (what the Arduino ESP.get*Heap() report).
MemInfo memInfo();
// Byte addressable data RAM: where malloc() and the Arena live.
MemInfo memInfoData();
// Per region listing of the data RAM, on the console.
void memDump();

// RAM a DMA engine can read. Free with free().
void *dmaAlloc(uint32_t bytes);

// Console (serial or USB) ready for printf. Takes up to 2.2 s on chips with
// USB.
void consoleInit();

// Chip, SDK, flash and heap lines of the boot banner.
void printPlatformInfo();

// Checksum and hash of the running firmware image. supported is false where
// the platform cannot do it (result then meaningless).
bool imageVerify(bool &supported);

// Hardware random number.
uint32_t random32();

// The last reset was a crash (panic or watchdog).
bool resetWasPanic();

} // namespace hal

#endif // HAL_ESP_SYS_H
