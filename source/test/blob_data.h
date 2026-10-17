#ifndef TEST_BLOB_DATA_H
#define TEST_BLOB_DATA_H

#include <cstdlib>
#include "../src/emulation/asset.h"

// A blob's elements without the Arena: for tests that hold several games
// at once (each game's planned Arena blocks assume it runs alone).
template <typename T> const T *blobData(const FlashAsset<T, PLAIN> &b) {
  return b.data;
}

template <typename T> const T *blobData(const FlashAsset<T, COMPRESSED> &b) {
  T *buf = (T *)malloc(b.count * sizeof(T));
  Asset<T, COMPRESSED>(b.packed, b.packedLen, b.count).decodeInto(buf);
  return buf;
}

#endif
