#include "flash_region.h"
#include <esp_partition.h>
#include "rtos.h"

namespace hal {

static constexpr esp_partition_subtype_t ASSETS_SUBTYPE = (esp_partition_subtype_t)0x40;
static constexpr uint32_t ERASE_STEP = 64 * 1024;

typedef esp_partition_mmap_handle_t MmapHandle;
static constexpr esp_partition_mmap_flag_t MMAP_DATA = ESP_PARTITION_MMAP_DATA;
static void munmap(MmapHandle handle) { esp_partition_munmap(handle); }
static_assert(sizeof(MmapHandle) == sizeof(uint32_t), "handle stored as uint32_t");

const void *FlashRegion::partition() const {
  if (!part) {
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ASSETS_SUBTYPE, label);
  }
  return part;
}

uint32_t FlashRegion::size() const {
  const esp_partition_t *p = (const esp_partition_t *)partition();
  return p ? p->size : 0;
}

bool FlashRegion::erase(uint32_t offset, uint32_t length) {
  const esp_partition_t *p = (const esp_partition_t *)partition();
  if (!p) {
    return false;
  }
  while (length) {
    const uint32_t step = length < ERASE_STEP ? length : ERASE_STEP;
    if (esp_partition_erase_range(p, offset, step) != ESP_OK) {
      return false;
    }
    offset += step;
    length -= step;
    sleepMs(1);
  }
  return true;
}

bool FlashRegion::write(uint32_t offset, const uint8_t *data, uint32_t length) {
  const esp_partition_t *p = (const esp_partition_t *)partition();
  return p && esp_partition_write(p, offset, data, length) == ESP_OK;
}

const uint8_t *FlashRegion::map() {
  const esp_partition_t *p = (const esp_partition_t *)partition();
  if (mapped || !p) {
    return mapped;
  }
  const void *ptr = nullptr;
  MmapHandle h = 0;
  if (esp_partition_mmap(p, 0, p->size, MMAP_DATA, &ptr, &h) != ESP_OK) {
    return nullptr;
  }
  handle = h;
  mapped = (const uint8_t *)ptr;
  return mapped;
}

void FlashRegion::unmap() {
  if (!mapped) {
    return;
  }
  munmap(handle);
  mapped = nullptr;
}

} // namespace hal
