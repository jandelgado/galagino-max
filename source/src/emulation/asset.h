#ifndef ASSET_H
#define ASSET_H

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdio>       // host tests
#include <cstdlib>
#endif
#include <cstdint>
#include "arena.h"
#include "uzlib.h"

enum AssetMode { PLAIN, COMPRESSED };

// Plain flash array or zlib blob unpacked to heap on first access. Same
// interface either way, so converters can switch formats without touching
// call sites.
// `mode` picks the variant at compile time, so each carries only the
// state it needs.
template<typename T, AssetMode mode> class Asset;

// Flash-resident (constexpr) description of one converted array, emitted by
// romconv next to the array itself. A machine holds an Asset member built
// from it, so the unpack cache lives and dies with the machine instance.
template<typename T, AssetMode mode> struct FlashAsset;

template<typename T>
struct FlashAsset<T, PLAIN> {
    const T *data;
    uint32_t count;
};

template<typename T>
struct FlashAsset<T, COMPRESSED> {
    const uint8_t *packed;
    uint32_t packedLen;
    uint32_t count;
};

template<typename T>
class Asset<T, PLAIN> {
public:
    // Points into flash, no copy.
    constexpr Asset(const T *flashData, unsigned int count)
      : data_(flashData), count_(count) { }
    constexpr Asset(const FlashAsset<T, PLAIN> &blob)
      : data_(blob.data), count_(blob.count) { }

    Asset(const Asset &) = delete;
    Asset &operator=(const Asset &) = delete;

    const T *data() const { return data_; }
    operator const T* () const {return data_;}

    // Element count. sizeof(name) only sees the wrapper.
    unsigned int size() const { return count_; }

private:
    const T *data_;
    unsigned int count_;
};

template<typename T>
class Asset<T, COMPRESSED> {
public:
    // uzlib may write one byte past dest_limit, independent of T.
    static constexpr uint32_t OVERRUN_SLACK_BYTES = 1;

    // Unpacked on first access and cached.
    constexpr Asset(const uint8_t *packed, uint32_t packedLen, uint32_t count)
      : packed(packed), packedLen(packedLen), count(count), current(nullptr) { }
    constexpr Asset(const FlashAsset<T, COMPRESSED> &blob)
      : Asset(blob.packed, blob.packedLen, blob.count) { }

    // No release(): the cache dies with its machine, Arena::reset() frees
    // the memory. Menu logos are statics and use decodeInto().

    Asset(const Asset &) = delete;
    Asset &operator=(const Asset &) = delete;

    const T *data() const {
      if (!current) { unpack(); }
      return current;
    }
    operator const T*() const {return data();}

    // Element count. sizeof(name) only sees the wrapper.
    uint32_t size() const { return count; }

    // Decompress into a caller-owned buffer of size() elements, bypassing
    // the data() cache. Lets callers reuse fixed buffers; repeated
    // new[]/delete[] fragments the heap.
    void decodeInto(T *dst) const {
      // No dict: dest holds the whole output, so back-references read
      // from it. Saves a window buffer.
      struct uzlib_uncomp d;
      uzlib_uncompress_init(&d, nullptr, 0);
      d.source = packed;
      d.source_limit = packed + packedLen;
      d.source_read_cb = nullptr;

      // Returns window size on success, not a TINF_* status.
      int hdr = uzlib_zlib_parse_header(&d);
      d.dest_start = d.dest = (uint8_t *)dst;
      d.dest_limit = (uint8_t *)dst + count * sizeof(T);

      int status = hdr;
      if (hdr >= 0) {
        // With dest full, TINF_OK repeats until the end-of-block marker
        // and checksum are consumed.
        do {
          status = uzlib_uncompress_chksum(&d);
        } while (status == TINF_OK);
      }

      if (status != TINF_DONE || d.dest != d.dest_limit) {
        printf("Asset: decompress failed (status=%d, got %u/%u bytes)\n",
               status, (unsigned)(d.dest - d.dest_start), (unsigned)(count * sizeof(T)));
        abort();
      }
    }

private:
    void unpack() const {
#ifdef ARDUINO
      uint32_t t0 = millis();
#endif
      // uzlib writes literals without a bounds check and can write one byte
      // past dest_limit. Pad so that byte does not corrupt the arena.
      T *buf = (T *)Arena::alloc(count * sizeof(T) + OVERRUN_SLACK_BYTES, alignof(T));
      // read now: the other core may allocate during decodeInto()
      uint32_t usedAfterThisAlloc = Arena::bytesUsed();

      decodeInto(buf);

      uint32_t ms = 0;
#ifdef ARDUINO
      ms = millis() - t0;
#endif
      uint32_t decompressedBytes = count * sizeof(T);
      double ratio = 100.0 * (1.0 - (double)packedLen / (double)decompressedBytes);
      printf("Asset: unpacked %u bytes (packed %u, %.1f%% smaller) in %u ms. Arena used: %u/%u\n",
             decompressedBytes, packedLen, ratio, ms, (unsigned)usedAfterThisAlloc, (unsigned)Arena::CAPACITY);

      current = buf;
    }

    const uint8_t *packed;
    uint32_t packedLen;
    uint32_t count;
    mutable const T *current;
};

#endif
