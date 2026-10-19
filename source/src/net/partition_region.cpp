#ifdef ASSETS_REMOTE
#include <Arduino.h>
#include "partition_region.h"

static constexpr esp_partition_subtype_t ASSETS_SUBTYPE = (esp_partition_subtype_t)0x40;
static constexpr uint32_t ERASE_STEP = 64 * 1024;

// looked up on first use, not at static init
const esp_partition_t *EspPartitionRegion::partition() const {
  if (!part) {
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ASSETS_SUBTYPE, label);
  }
  return part;
}

uint32_t EspPartitionRegion::size() const {
  return partition() ? partition()->size : 0;
}

bool EspPartitionRegion::erase(uint32_t offset, uint32_t length) {
  if (!partition()) {
    return false;
  }
  while (length) {
    const uint32_t step = length < ERASE_STEP ? length : ERASE_STEP;
    if (esp_partition_erase_range(part, offset, step) != ESP_OK) {
      return false;
    }
    offset += step;
    length -= step;
    vTaskDelay(1);
  }
  return true;
}

bool EspPartitionRegion::write(uint32_t offset, const uint8_t *data, uint32_t length) {
  return partition() && esp_partition_write(part, offset, data, length) == ESP_OK;
}

const uint8_t *EspPartitionRegion::map() {
  if (mapped || !partition()) {
    return mapped;
  }
  const void *p = nullptr;
  if (esp_partition_mmap(part, 0, part->size, SPI_FLASH_MMAP_DATA, &p, &handle) != ESP_OK) {
    return nullptr;
  }
  mapped = (const uint8_t *)p;
  return mapped;
}

void EspPartitionRegion::unmap() {
  if (!mapped) {
    return;
  }
  spi_flash_munmap(handle);
  mapped = nullptr;
}
#endif
