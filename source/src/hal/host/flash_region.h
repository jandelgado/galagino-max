#ifndef HAL_HOST_FLASH_REGION_H
#define HAL_HOST_FLASH_REGION_H

#include <stdint.h>
#include <string.h>
#include <vector>

namespace hal {

// RAM fake of NOR flash: erase sets 0xFF in 4 KB sectors, a write may only
// hit erased bytes. Starts as all 0x00 (never erased). The test reads mem
// and eraseWhileMapped.
class FlashRegion {
public:
  static constexpr uint32_t SECTOR = 4096;
  static constexpr uint32_t DEFAULT_BYTES = 1024 * 1024;

  explicit FlashRegion(const char *, uint32_t bytes = DEFAULT_BYTES) : mem(bytes, 0x00) { }

  uint32_t size() const { return (uint32_t)mem.size(); }

  bool erase(uint32_t offset, uint32_t length) {
    if (mapped) {
      eraseWhileMapped = true;
    }
    if (offset % SECTOR || length % SECTOR || offset + length > mem.size()) {
      return false;
    }
    memset(&mem[offset], 0xFF, length);
    return true;
  }

  bool write(uint32_t offset, const uint8_t *data, uint32_t length) {
    if (offset + length > mem.size()) {
      return false;
    }
    for (uint32_t i = 0; i < length; i++) {
      if (mem[offset + i] != 0xFF) {
        return false;
      }
      mem[offset + i] = data[i];
    }
    return true;
  }

  const uint8_t *map() {
    mapped = true;
    return mem.data();
  }

  void unmap() { mapped = false; }

  std::vector<uint8_t> mem;
  bool mapped = false;
  bool eraseWhileMapped = false;
};

} // namespace hal

#endif // HAL_HOST_FLASH_REGION_H
