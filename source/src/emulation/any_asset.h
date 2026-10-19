#ifndef ANY_ASSET_H
#define ANY_ASSET_H

#include <cstddef>
#include "asset.h"

// For base classes fed by subclass blobs (system1, starforce): the mode of
// each blob comes from romconv per game, so the base class takes either.

// One blob as romconv emitted it: an LZMA blob (unpacked into the Arena)
// or a PLAIN array. Converts from either, so an initializer lists blobs as
// they come.
template <typename T> struct AssetRef {
  const FlashAsset<T, COMPRESSED> *packed;
  const FlashAsset<T, PLAIN> *plain;
  AssetRef(const FlashAsset<T, COMPRESSED> &b) : packed(&b), plain(nullptr) {}
  AssetRef(const FlashAsset<T, PLAIN> &b) : packed(nullptr), plain(&b) {}
  AssetRef(std::nullptr_t = nullptr) : packed(nullptr), plain(nullptr) {}
};

template <typename T> class AnyAsset {
public:
  // Asset<PLAIN> resolves remote blobs (ASSETS_REMOTE) in its ctor
  explicit AnyAsset(const AssetRef<T> &r)
      : packed(r.packed ? *r.packed : FlashAsset<T, COMPRESSED>{nullptr, 0, 0, ARENA_ANY}),
        plain(r.plain ? *r.plain : FlashAsset<T, PLAIN>{nullptr, 0, 0}) {}

  // nullptr for an omitted asset or a remote one missing from the bundle
  const T *data() const { return plain.size() ? plain.data() : (packed.size() ? packed.data() : nullptr); }
  uint32_t size() const { return plain.size() ? plain.size() : packed.size(); }

private:
  Asset<T, COMPRESSED> packed;
  Asset<T, PLAIN> plain;
};

#endif
