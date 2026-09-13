#ifndef ROMDATA_H
#define ROMDATA_H

#include <cstdio>
#include <cstdlib>
#include <new>
#include "uzlib.h"

// Plain flash array or zlib blob unpacked to heap on first access. Same
// interface either way, so converters can switch formats without touching
// call sites.
template<typename T>
class RomData {
public:
    // Plain: points into flash, no copy.
    constexpr RomData(const T *flashData, unsigned int count)
      : packed(nullptr), packedLen(0), count(count), current(flashData) { }

    // Compressed: unpacked on first access, kept until release().
    constexpr RomData(const unsigned char *packed, unsigned int packedLen, unsigned int count)
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

    const T &operator[](unsigned int idx) const { return data()[idx]; }

    // Safe no-op for a plain (flash-resident) instance: packed is null there,
    // so this never touches `current`, which points at flash we don't own.
    void release() {
      if (packed) {
        delete[] current;
        current = nullptr;
      }
    }

private:
    void unpack() const {
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
      d.dest_start = d.dest = (unsigned char *)buf;
      d.dest_limit = (unsigned char *)buf + count * sizeof(T);

      int status = hdr;
      if (hdr >= 0) {
        // With dest full, TINF_OK repeats until the end-of-block marker
        // and checksum are consumed.
        do {
          status = uzlib_uncompress_chksum(&d);
        } while (status == TINF_OK);
      }

      if (status != TINF_DONE || d.dest != d.dest_limit) {
        printf("RomData: decompress failed (status=%d, got %lu/%lu bytes)\n",
               status, (unsigned long)(d.dest - d.dest_start), (unsigned long)(count * sizeof(T)));
        abort();
      }
      current = buf;
    }

    const unsigned char *packed;
    unsigned int packedLen;
    unsigned int count;
    mutable const T *current;
};

#endif
