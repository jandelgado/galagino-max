#ifndef PARTITION_REGION_H
#define PARTITION_REGION_H

#ifdef ASSETS_REMOTE
#include <esp_partition.h>
#include <esp_spi_flash.h>
#include "flash_region.h"

// The data partition holding downloaded bundles (partitions-assets.csv:
// label "assets", subtype 0x40).
class EspPartitionRegion : public FlashRegion {
public:
  explicit EspPartitionRegion(const char *label) : label(label) { }

  uint32_t size() const override;
  // in 64 KB steps with a yield: a 720 KB erase must not starve the idle
  // task watchdog (5 s)
  bool erase(uint32_t offset, uint32_t length) override;
  bool write(uint32_t offset, const uint8_t *data, uint32_t length) override;
  const uint8_t *map() override;
  void unmap() override;

private:
  const esp_partition_t *partition() const;

  const char *label;
  mutable const esp_partition_t *part = nullptr;
  const uint8_t *mapped = nullptr;
  spi_flash_mmap_handle_t handle = 0;
};
#endif

#endif
