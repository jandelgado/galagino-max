#include <cstring>
#include "asset_store.h"

static constexpr uint32_t TAG_MAGIC = 0x54474C47;   // "GLGT"
static constexpr uint32_t COMMIT_WORD = 0x434F4D54; // "TMOC"
static constexpr uint32_t SECTOR = 4096;

struct Tag {
  uint32_t magic;
  uint32_t length;
  uint8_t hash[8];
  uint32_t commit;
};

bool AssetStore::holds(const BundleRef &ref) {
  const uint8_t *base = region.size() >= TAG_BYTES ? region.map() : nullptr;
  if (!base) {
    return false;
  }
  Tag tag;
  memcpy(&tag, base, sizeof(tag));
  if (tag.magic != TAG_MAGIC || tag.commit != COMMIT_WORD) {
    return false;
  }
  return tag.length == ref.wireLength && !memcmp(tag.hash, ref.hash, sizeof(tag.hash));
}

bool AssetStore::begin(const BundleRef &ref) {
  written = 0;
  pending = {};
  if (region.size() < TAG_BYTES || ref.wireLength > region.size() - TAG_BYTES) {
    return false;
  }

  // erasing a mapped region would leave stale data in the flash cache
  region.unmap();
  const uint32_t dataBytes = (ref.wireLength + SECTOR - 1) / SECTOR * SECTOR;
  if (!region.erase(0, TAG_BYTES + dataBytes)) {
    return false;
  }
  pending = ref;
  return true;
}

bool AssetStore::append(const uint8_t *data, uint32_t length) {
  if (length > pending.wireLength - written) {
    return false;
  }
  if (!region.write(TAG_BYTES + written, data, length)) {
    return false;
  }
  written += length;
  return true;
}

bool AssetStore::commit() {
  if (!pending.wireLength || written != pending.wireLength) {
    return false;
  }
  Tag tag = {TAG_MAGIC, pending.wireLength, {}, COMMIT_WORD};
  memcpy(tag.hash, pending.hash, sizeof(tag.hash));
  return region.write(0, (const uint8_t *)&tag, sizeof(tag));
}

const uint8_t *AssetStore::bundle() {
  const uint8_t *base = region.map();
  return base ? base + TAG_BYTES : nullptr;
}
