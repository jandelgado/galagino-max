#ifndef HAL_HOST_SYS_H
#define HAL_HOST_SYS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
  uint32_t largest;
  uint32_t minFree;
};

inline void sysInfo(SysInfo &out) {
  out.chip = "host";
  out.revision = 0;
  out.cores = 1;
  out.cpuMhz = 0;
  out.flashBytes = 0;
  out.sdkVersion = "host";
}

inline MemInfo memInfo() { return MemInfo{0, 0, 0}; }
inline MemInfo memInfoData() { return MemInfo{0, 0, 0}; }
inline void memDump() {}
inline void *allocSpare(uint32_t bytes) { return malloc(bytes); }
inline void consoleInit() {}
inline void printPlatformInfo() { printf("host\n"); }
inline bool imageVerify(bool &supported) {
  supported = false;
  return false;
}
inline uint32_t random32() { return (uint32_t)rand(); }
inline bool resetWasPanic() { return false; }

} // namespace hal

#endif // HAL_HOST_SYS_H
