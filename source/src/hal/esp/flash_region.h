#ifndef HAL_ESP_FLASH_REGION_H
#define HAL_ESP_FLASH_REGION_H

#include <stdint.h>

namespace hal {

// Data partition found by label (subtype 0x40, pio/partitions/partitions-assets.csv).
// Looked up on first use, not at static init.
class FlashRegion {
public:
  explicit FlashRegion(const char *label) : label(label) { }

  // 0 if the partition does not exist
  uint32_t size() const;
  // offset and length 4 KB aligned; in 64 KB steps with a sleep, so a long
  // erase does not starve the idle task watchdog
  bool erase(uint32_t offset, uint32_t length);
  bool write(uint32_t offset, const uint8_t *data, uint32_t length);
  // whole region; stays valid until unmap(). nullptr on failure
  const uint8_t *map();
  void unmap();

private:
  const void *partition() const;

  const char *label;
  mutable const void *part = nullptr;
  const uint8_t *mapped = nullptr;
  uint32_t handle = 0;
};

} // namespace hal

#endif // HAL_ESP_FLASH_REGION_H
