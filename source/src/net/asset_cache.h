#ifndef ASSET_CACHE_H
#define ASSET_CACHE_H

#include <cstdint>
#include "bundle.h"
#include "flash_region.h"

// Downloaded cache entries (bundle_unpack.h) in a flash region, oldest
// download evicted first. Each entry: 4 KB tag sector, then the entry,
// rounded up to 4 KB. A new entry goes right after the newest one, or to
// offset 0 if it does not fit before the end, so overwriting walks the
// region in download order:
//
//   [tag|entry 3 ][tag|entry 4    ]       [tag|entry 2       ]
//   ^ wrapped                     ^ next  ^ oldest, next to go
//
// Tag: magic, seq, length, hash[8], commit word written last. An entry
// counts only once committed: a power loss mid-download leaves none.
// Committed entries never overlap: begin() erases the tag of every entry the
// new slot touches before it erases the slot.
class AssetCache {
public:
  static constexpr uint32_t TAG_BYTES = 4096;

  explicit AssetCache(FlashRegion &region) : region(region) { }

  // committed entry with this hash and entryLength, mapped; nullptr if none
  const uint8_t *find(const BundleRef &ref);
  // picks and erases the slot; false if entryLength + TAG_BYTES exceeds
  // the region or flash fails
  bool begin(const BundleRef &ref);
  bool append(const uint8_t *data, uint32_t length);
  // writes the tag; false unless entryLength bytes were appended
  bool commit();

private:
  struct Tag {
    uint32_t magic;
    uint32_t seq;
    uint32_t length;
    uint8_t hash[8];
    uint32_t commit;
  };

  static uint32_t span(uint32_t length);
  bool tagAt(const uint8_t *base, uint32_t pos, Tag &tag) const;
  bool overlapped(uint32_t from, uint32_t to, uint32_t &at);
  bool evict(uint32_t from, uint32_t to);

  FlashRegion &region;
  BundleRef pending = {};
  uint32_t slot = 0;
  uint32_t seq = 0;
  uint32_t written = 0;
};

#endif
