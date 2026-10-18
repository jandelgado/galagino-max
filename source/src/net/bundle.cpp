#include <cstring>
#include "bundle.h"

static constexpr uint32_t HEADER_BYTES = 8;
static constexpr uint32_t ENTRY_BYTES = 20;
static constexpr uint32_t ALIGN = 4;      // uint32_t assets read in place

struct Entry {
  uint32_t id;
  uint32_t offset;
  uint32_t stored;
  uint32_t raw;
  uint32_t flags;
};

// C++11: odr-used static constexpr members need a definition
constexpr uint32_t Bundle::FLAG_RAW;
constexpr uint32_t Bundle::FLAG_PACKED;
constexpr uint32_t Bundle::FLAG_LZMA;
constexpr uint32_t Bundle::MAGIC;
constexpr uint16_t Bundle::VERSION;

static const uint8_t *mounted = nullptr;
static uint16_t entryCount = 0;

static uint32_t read32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static Entry entryAt(const uint8_t *base, uint16_t i) {
  const uint8_t *p = base + HEADER_BYTES + i * ENTRY_BYTES;
  return {read32(p), read32(p + 4), read32(p + 8), read32(p + 12), read32(p + 16)};
}

// every entry inside the bundle, aligned, a cache entry form
static bool entryValid(const Entry &e, uint32_t length) {
  if (e.offset % ALIGN) {
    return false;
  }
  if (e.flags == Bundle::FLAG_RAW && e.stored != e.raw) {
    return false;
  }
  if (e.flags != Bundle::FLAG_RAW && e.flags != Bundle::FLAG_PACKED) {
    return false;
  }
  return e.offset <= length && e.stored <= length - e.offset;
}

bool Bundle::mount(const uint8_t *base, uint32_t length) {
  unmount();
  if (length < HEADER_BYTES || read32(base) != MAGIC) {
    return false;
  }
  if ((base[4] | (base[5] << 8)) != VERSION) {
    return false;
  }

  const uint16_t count = base[6] | (base[7] << 8);
  if (HEADER_BYTES + (uint32_t)count * ENTRY_BYTES > length) {
    return false;
  }
  for (uint16_t i = 0; i < count; i++) {
    if (!entryValid(entryAt(base, i), length)) {
      return false;
    }
  }

  mounted = base;
  entryCount = count;
  return true;
}

void Bundle::unmount() {
  mounted = nullptr;
  entryCount = 0;
}

const void *Bundle::find(uint32_t id, uint32_t flags) {
  for (uint16_t i = 0; i < entryCount; i++) {
    const Entry e = entryAt(mounted, i);
    if (e.id == id) {
      return e.flags == flags ? mounted + e.offset : nullptr;
    }
  }
  return nullptr;
}
