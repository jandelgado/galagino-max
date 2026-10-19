#ifndef BUNDLE_H
#define BUNDLE_H

#include <cstdint>

// Identity of a game's remote asset bundle, emitted by romconv
// (<game>_bundle.h). hash: first 8 bytes of SHA-256, URL /g/<hex>.
struct BundleRef {
  uint8_t hash[8];
  uint32_t length;
};

// The mounted bundle: header + PLAIN asset data, memory-mapped from the
// assets partition. Asset<PLAIN> looks its id up here once, in its ctor.
// Layout: romconv/internal/pyconv/bundle.py.
class Bundle {
public:
  // False, and nothing mounted, if the header is invalid.
  static bool mount(const uint8_t *base, uint32_t length);
  static void unmount();

  // nullptr if nothing is mounted, the id is missing or rawLen differs.
  static const void *find(uint32_t id, uint32_t rawLen);
};

#endif
