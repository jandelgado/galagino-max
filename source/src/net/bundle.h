#ifndef BUNDLE_H
#define BUNDLE_H

#include <cstdint>

// Identity of a game's remote asset bundle, emitted by romconv
// (<game>_bundle.h). hash: first 8 bytes of SHA-256 of the wire bundle, URL
// /g/<hex>. entryLength: its decoded cache entry. name: loader screen.
struct BundleRef {
  uint8_t hash[8];
  uint32_t wireLength;
  uint32_t entryLength;
  const char *name;
};

// The mounted cache entry, memory-mapped from the assets partition. Asset
// looks its id up here once, in its ctor. Layout:
// romconv/romconv/remote/bundle.py.
class Bundle {
public:
  // entry flags: RAW read in place (Asset<PLAIN>), PACKED unpacked into
  // Arena (Asset<COMPRESSED>); LZMA only on the wire (bundle_unpack.h)
  static constexpr uint32_t FLAG_RAW = 0;
  static constexpr uint32_t FLAG_PACKED = 1;
  static constexpr uint32_t FLAG_LZMA = 2;
  static constexpr uint32_t MAGIC = 0x42474C47;  // "GLGB"
  static constexpr uint16_t VERSION = 2;

  // False, and nothing mounted, if the header is invalid.
  static bool mount(const uint8_t *base, uint32_t length);
  static void unmount();

  // nullptr if nothing is mounted, the id is missing or its flags differ.
  // No length check: ADPCM PLAIN blobs count samples, not bytes; the app
  // pins the bundle hash, so ids and data always match.
  static const void *find(uint32_t id, uint32_t flags);
};

#endif
