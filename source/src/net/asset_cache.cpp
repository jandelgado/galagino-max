#include <cstring>
#include "asset_cache.h"

static constexpr uint32_t TAG_MAGIC = 0x43474C47;   // "GLGC"
static constexpr uint32_t COMMIT_WORD = 0x434F4D54; // "TMOC"
static constexpr uint32_t SECTOR = 4096;

// tag sector + entry, whole sectors
uint32_t AssetCache::span(uint32_t length) {
  return TAG_BYTES + (length + SECTOR - 1) / SECTOR * SECTOR;
}

// committed tag at pos whose entry fits the region
bool AssetCache::tagAt(const uint8_t *base, uint32_t pos, Tag &tag) const {
  memcpy(&tag, base + pos, sizeof(tag));
  if (tag.magic != TAG_MAGIC || tag.commit != COMMIT_WORD) {
    return false;
  }
  return tag.length <= region.size() - TAG_BYTES && pos + span(tag.length) <= region.size();
}

const uint8_t *AssetCache::find(const BundleRef &ref) {
  const uint8_t *base = region.size() >= TAG_BYTES ? region.map() : nullptr;
  if (!base) {
    return nullptr;
  }

  // hop over entries, sector by sector over free space
  Tag tag;
  for (uint32_t pos = 0; pos + TAG_BYTES <= region.size();) {
    if (!tagAt(base, pos, tag)) {
      pos += SECTOR;
      continue;
    }
    if (tag.length == ref.entryLength && !memcmp(tag.hash, ref.hash, sizeof(tag.hash))) {
      return base + pos + TAG_BYTES;
    }
    pos += span(tag.length);
  }
  return nullptr;
}

// first committed entry touching [from, to)
bool AssetCache::overlapped(uint32_t from, uint32_t to, uint32_t &at) {
  const uint8_t *base = region.map();
  if (!base) {
    return false;
  }

  Tag tag;
  for (uint32_t pos = 0; pos + TAG_BYTES <= region.size();) {
    if (!tagAt(base, pos, tag)) {
      pos += SECTOR;
      continue;
    }
    if (pos < to && from < pos + span(tag.length)) {
      at = pos;
      return true;
    }
    pos += span(tag.length);
  }
  return false;
}

// tags first: an entry cut by the slot must not survive a power loss
bool AssetCache::evict(uint32_t from, uint32_t to) {
  uint32_t at;
  while (overlapped(from, to, at)) {
    // erasing a mapped region would leave stale data in the flash cache
    region.unmap();
    if (!region.erase(at, SECTOR)) {
      return false;
    }
  }
  region.unmap();
  return region.erase(from, to - from);
}

bool AssetCache::begin(const BundleRef &ref) {
  written = 0;
  pending = {};
  if (region.size() < TAG_BYTES || ref.entryLength > region.size() - TAG_BYTES) {
    return false;
  }
  const uint8_t *base = region.map();
  if (!base) {
    return false;
  }

  // after the newest entry, else wrap to 0
  uint32_t newestEnd = 0;
  seq = 0;
  Tag tag;
  for (uint32_t pos = 0; pos + TAG_BYTES <= region.size();) {
    if (!tagAt(base, pos, tag)) {
      pos += SECTOR;
      continue;
    }
    if (tag.seq >= seq) {
      seq = tag.seq + 1;
      newestEnd = pos + span(tag.length);
    }
    pos += span(tag.length);
  }
  const uint32_t need = span(ref.entryLength);
  slot = newestEnd + need <= region.size() ? newestEnd : 0;

  if (!evict(slot, slot + need)) {
    return false;
  }
  pending = ref;
  return true;
}

bool AssetCache::append(const uint8_t *data, uint32_t length) {
  if (length > pending.entryLength - written) {
    return false;
  }
  if (!region.write(slot + TAG_BYTES + written, data, length)) {
    return false;
  }
  written += length;
  return true;
}

bool AssetCache::commit() {
  if (!pending.entryLength || written != pending.entryLength) {
    return false;
  }
  Tag tag = {TAG_MAGIC, seq, pending.entryLength, {}, COMMIT_WORD};
  memcpy(tag.hash, pending.hash, sizeof(tag.hash));
  pending = {};
  // commit word is the last field: written last
  return region.write(slot, (const uint8_t *)&tag, sizeof(tag));
}
