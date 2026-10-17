#ifndef FLASH_REGION_H
#define FLASH_REGION_H

#include <cstdint>

// Erasable, writable, memory-mappable flash area: the assets partition on
// the device, a RAM fake in host tests.
class FlashRegion {
public:
  virtual ~FlashRegion() { }
  virtual uint32_t size() const = 0;
  // offset and length 4 KB aligned
  virtual bool erase(uint32_t offset, uint32_t length) = 0;
  virtual bool write(uint32_t offset, const uint8_t *data, uint32_t length) = 0;
  // whole region; stays valid until unmap()
  virtual const uint8_t *map() = 0;
  virtual void unmap() = 0;
};

#endif
