#ifndef ASSET_STORE_H
#define ASSET_STORE_H

#include <cstdint>
#include "bundle.h"
#include "flash_region.h"

// One downloaded bundle in a flash region. A bundle counts only once its
// tag is written, and the tag is written last: an interrupted download
// (power loss, network) leaves no valid bundle.
//
//   region: [ tag sector 4 KB ][ bundle bytes ... ]
//   tag:    magic, length, hash[8], commit word
class AssetStore {
public:
  static constexpr uint32_t TAG_BYTES = 4096;

  explicit AssetStore(FlashRegion &region) : region(region) { }

  // committed tag matches hash and length
  bool holds(const BundleRef &ref);
  // unmaps, then erases tag and data area; false if the bundle does not fit
  bool begin(const BundleRef &ref);
  bool append(const uint8_t *data, uint32_t length);
  // writes the tag; false unless every byte was appended
  bool commit();
  // nullptr if the region cannot be mapped
  const uint8_t *bundle();

private:
  FlashRegion &region;
  BundleRef pending = {};
  uint32_t written = 0;
};

#endif
