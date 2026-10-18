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
#include "lzma/LzmaDec.h"
#ifdef ASSETS_REMOTE
#include "../catalog.h"     // BUILTIN_<DIR> of the generated asset headers
#include "../net/bundle.h"
#endif

enum AssetMode { PLAIN, COMPRESSED };

// Plain flash array or LZMA blob unpacked to the Arena on first access. Same
// interface either way, so converters can switch formats without touching
// call sites.
// `mode` picks the variant at compile time, so each carries only the
// state it needs.
template<typename T, AssetMode mode> class Asset;

// Flash-resident (constexpr) description of one converted array, emitted by
// romconv next to the array itself. A machine holds an Asset member built
// from it, so the unpack cache lives and dies with the machine instance.
template<typename T, AssetMode mode> struct FlashAsset;

// data == nullptr: remote asset (ASSETS_REMOTE), resolved by id in the
// mounted bundle. Built-in blobs leave id 0.
template<typename T>
struct FlashAsset<T, PLAIN> {
    const T *data;
    uint32_t count;
    uint32_t id;
};

// block: where unpack() puts it (omitted: ARENA_ANY, first fit). Shares
// count's word: ~400 blobs in flash.
// packed == nullptr: remote asset (ASSETS_REMOTE), resolved by id in the
// mounted bundle. id only exists in ASSETS_REMOTE builds: mode 1 blobs keep
// their size.
template<typename T>
struct FlashAsset<T, COMPRESSED> {
    const uint8_t *packed;
    uint32_t packedLen;
    uint32_t count : 30;
    ArenaBlock block : 2;
#ifdef ASSETS_REMOTE
    uint32_t id;
#endif
};

template<typename T>
class Asset<T, PLAIN> {
public:
    // Points into flash, no copy.
    constexpr Asset(const T *flashData, unsigned int count)
      : data_(flashData), count_(count) { }
#ifdef ASSETS_REMOTE
    // once per machine, never in data(): no per-frame cost
    constexpr Asset(const FlashAsset<T, PLAIN> &blob)
      : data_(blob.data ? blob.data : (const T *)Bundle::find(blob.id, Bundle::FLAG_RAW)),
        count_(blob.count) { }
#else
    constexpr Asset(const FlashAsset<T, PLAIN> &blob)
      : data_(blob.data), count_(blob.count) { }
#endif

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
    // Unpacked on first access and cached.
    constexpr Asset(const uint8_t *packed, uint32_t packedLen, uint32_t count,
                    ArenaBlock block = ARENA_ANY)
      : packed(packed), packedLen(packedLen), count(count), block(block), current(nullptr) { }
#ifdef ASSETS_REMOTE
    // once per machine; id 0: no asset (AnyAsset's empty side)
    Asset(const FlashAsset<T, COMPRESSED> &blob)
      : Asset(blob.packed || !blob.id ? blob.packed : (const uint8_t *)Bundle::find(blob.id, Bundle::FLAG_PACKED),
              blob.packedLen, blob.count, blob.block) { }
#else
    constexpr Asset(const FlashAsset<T, COMPRESSED> &blob)
      : Asset(blob.packed, blob.packedLen, blob.count, blob.block) { }
#endif

    // No release(): the cache dies with its machine, Arena::reset() frees
    // the memory. Menu logos (logo.h) use decodeInto().

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
    //
    // Packed layout (romconv asset_emit.py): 1 LZMA properties byte
    // (lc/lp/pb), then a raw LZMA1 stream with end marker.
    void decodeInto(T *dst) const {
#ifdef ASSETS_REMOTE
      if (!packed) {
        printf("Asset: remote asset missing in the mounted bundle\n");
        abort();
      }
#endif
      const SizeT outLen = count * sizeof(T);

      // dst holds the whole output, so back-references read from it: no
      // dictionary buffer. Dictionary size only bounds match distances.
      Byte props[LZMA_PROPS_SIZE] = {
        packed[0], (Byte)outLen, (Byte)(outLen >> 8), (Byte)(outLen >> 16), (Byte)(outLen >> 24)
      };

      SizeT destLen = outLen;
      SizeT srcLen = packedLen - 1;
      ELzmaStatus status;
      // probability table only, 5.5 KB at lc=lp=0, freed before return
      SRes res = LzmaDecode((Byte *)dst, &destLen, packed + 1, &srcLen,
                            props, LZMA_PROPS_SIZE, LZMA_FINISH_END, &status, &heapAlloc);

      if (res != SZ_OK || destLen != outLen) {
        printf("Asset: decompress failed (res=%d, status=%d, got %u/%u bytes)\n",
               (int)res, (int)status, (unsigned)destLen, (unsigned)outLen);
        abort();
      }
    }

private:
    void unpack() const {
#ifdef ARDUINO
      uint32_t t0 = millis();
#endif
      T *buf = (T *)Arena::alloc(count * sizeof(T), alignof(T), block);
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

    static void *heapAllocFn(ISzAllocPtr, size_t size) { return malloc(size); }
    static void heapFreeFn(ISzAllocPtr, void *address) { free(address); }
    static constexpr ISzAlloc heapAlloc = { heapAllocFn, heapFreeFn };

    const uint8_t *packed;
    uint32_t packedLen;
    uint32_t count;
    ArenaBlock block;
    mutable const T *current;
};

template<typename T>
constexpr ISzAlloc Asset<T, COMPRESSED>::heapAlloc;

#endif
