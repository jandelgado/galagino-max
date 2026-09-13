#ifndef ROMDATA_H
#define ROMDATA_H

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdio>       // host tests
#include <cstdlib>
#endif
#include <cstdint>
#include <new>
#include "uzlib.h"

enum eMode { PLAIN, COMPRESSED };

// Plain flash array or zlib blob unpacked to heap on first access. Same
// interface either way, so converters can switch formats without touching
// call sites.
// `mode` picks the variant at compile time, so each carries only the
// state it needs.
template<typename T, eMode mode> class RomData;

template<typename T>
class RomData<T, PLAIN> {
public:
    // Points into flash, no copy.
    constexpr RomData(const T *flashData, unsigned int count)
      : data_(flashData) { }

    RomData(const RomData &) = delete;
    RomData &operator=(const RomData &) = delete;

    const T *data() const { return data_; }
    const T &operator[](unsigned int idx) const { return data_[idx]; }

    // No-op: nothing owned. Kept so callers can treat every RomData
    // instance the same at machine-teardown time regardless of mode.
    void release() { }

private:
    const T *data_;
};

template<typename T>
class RomData<T, COMPRESSED> {
public:
    // Unpacked on first access and cached.
    constexpr RomData(const uint8_t *packed, uint32_t packedLen, uint32_t count)
      : packed(packed), packedLen(packedLen), count(count), current(nullptr) { }

    // Must stay trivial. A non-trivial destructor registers every global
    // instance for atexit, which keeps its arrays past --gc-sections and
    // duplicates their flash use. Free with release().
    ~RomData() = default;

    RomData(const RomData &) = delete;
    RomData &operator=(const RomData &) = delete;

    const T *data() const {
      if (!current) { unpack(); }
      return current;
    }

    const T &operator[](uint32_t idx) const { return data()[idx]; }

    void release() {
      delete[] current;
      current = nullptr;
    }

private:
    void unpack() const {
#ifdef ARDUINO
      uint32_t t0 = millis();
#endif

      T *buf = new (std::nothrow) T[count];
      if (!buf) {
        printf("RomData: allocation failed (%u bytes)\n", (unsigned)(count * sizeof(T)));
        abort();
      }

      // No dict: dest holds the whole output, so back-references read
      // from it. Saves a window buffer.
      struct uzlib_uncomp d;
      uzlib_uncompress_init(&d, nullptr, 0);
      d.source = packed;
      d.source_limit = packed + packedLen;
      d.source_read_cb = nullptr;

      // Returns window size on success, not a TINF_* status.
      int hdr = uzlib_zlib_parse_header(&d);
      d.dest_start = d.dest = (uint8_t *)buf;
      d.dest_limit = (uint8_t *)buf + count * sizeof(T);

      int status = hdr;
      if (hdr >= 0) {
        // With dest full, TINF_OK repeats until the end-of-block marker
        // and checksum are consumed.
        do {
          status = uzlib_uncompress_chksum(&d);
        } while (status == TINF_OK);
      }

      if (status != TINF_DONE || d.dest != d.dest_limit) {
        printf("RomData: decompress failed (status=%d, got %u/%u bytes)\n",
               status, (unsigned)(d.dest - d.dest_start), (unsigned)(count * sizeof(T)));
        abort();
      }

      uint32_t ms = 0;
#ifdef ARDUINO
      ms = millis() - t0;
#endif
      uint32_t decompressedBytes = count * sizeof(T);
      double ratio = 100.0 * (1.0 - (double)packedLen / (double)decompressedBytes);
      printf("RomData: unpacked %u bytes (packed %u, %.1f%% smaller) in %u ms\n",
             decompressedBytes, packedLen, ratio, ms);

      current = buf;
    }

    const uint8_t *packed;
    uint32_t packedLen;
    uint32_t count;
    mutable const T *current;
};

#endif
