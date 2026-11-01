#ifndef HAL_RP2350_FLASH_REGION_H
#define HAL_RP2350_FLASH_REGION_H

#include <stdint.h>

namespace hal {

// The flash between the end of the image (4 KB up) and the end the link
// leaves free (GALAGINO_FLASH_END: the Bluetooth key bank). One region, the
// label is ignored. Reads go through the XIP cache; erase and write park the
// emulation core in RAM with only the DVI scanout IRQ enabled (flash_region.cpp).
class FlashRegion {
public:
  explicit FlashRegion(const char *) { }

  uint32_t size() const;
  // offset and length 4 KB aligned, sector by sector
  bool erase(uint32_t offset, uint32_t length);
  bool write(uint32_t offset, const uint8_t *data, uint32_t length);
  // whole region, valid until unmap() (XIP: always mapped)
  const uint8_t *map();
  void unmap() { }
};

} // namespace hal

#endif // HAL_RP2350_FLASH_REGION_H
